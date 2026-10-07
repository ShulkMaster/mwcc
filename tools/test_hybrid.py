"""Tests of tools/hybrid.py and tools/difftest.py (python tools/test_hybrid.py). The hybrid tests need a GC_2_7
build (build/GC_2_7/compiled); the compile tests also need wibo."""
import argparse
import hashlib
import json
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
from pathlib import Path
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).parent))
import difftest
import hybrid
from pe import PEFile

BUILT = (hybrid.COMPILED / "src/msl/string.c.obj").exists() and hybrid.ORIGINAL.exists()
WIBO = next((w for w in (difftest.WIBO, hybrid.ROOT / "build/tools/wibo") if w.exists()), None)

PROGRAM = r"""
typedef struct Node { int kind; float weight; struct Node *next; } Node;
static const char *names[] = { "zero", "one", "two", "three" };
extern int printf(const char *, ...);
static inline int square(int x) { return x * x; }
int classify(int x) {
    switch (x) {
    case 0: return 7; case 1: return 11; case 2: return 13; case 5: return 17; case 9: return 19;
    default: return square(x);
    }
}
float total(Node *node) {
    float sum = 0.0f;
    for (; node; node = node->next) sum += node->weight * (float)node->kind;
    return sum;
}
void report(Node *node, int n) {
    int i;
    for (i = 0; i < n; i++) printf("%s %d %f\n", names[i & 3], classify(i), (double)total(node));
}
"""


def selection(**options):
    defaults = dict(exact=False, all_sourced=False, source=[], function=[], exclude=[], exclude_file=[])
    return argparse.Namespace(**{**defaults, **options})


@unittest.skipUnless(BUILT, "needs a GC_2_7 build")
class HybridTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.program = hybrid.Program()
        cls.exact = hybrid.exact_functions()
        cls.keys = hybrid.select(cls.program, selection(exact=True), cls.exact)
        cls.image, cls.manifest = hybrid.build(cls.keys, cls.program)

    def test_deterministic(self):
        image, manifest = hybrid.build(self.keys, hybrid.Program())
        self.assertEqual(hashlib.sha1(image).hexdigest(), hashlib.sha1(self.image).hexdigest())
        self.assertEqual(manifest, self.manifest)

    def test_selection(self):
        keys = hybrid.select(self.program, selection(source=["src/msl/string.c"], exclude=["strcpy"]))
        self.assertTrue(keys and all(k.startswith("src/msl/string.c:") for k in keys))
        self.assertNotIn("src/msl/string.c:strcpy", keys)
        self.assertEqual(hybrid.select(self.program, selection(function=["strcpy"])), ["src/msl/string.c:strcpy"])
        self.assertEqual(hybrid.select(self.program, selection(source=["src/msl/string.c"],
                                                               exclude=["src/msl/string.c"])), [])

    def test_entries_jump_to_copies(self):
        pe = hybrid.PE(hybrid.ORIGINAL)
        hybrid_pe = PEImage(self.image)
        jumps = [i for i in self.manifest["injected"] if i["copy"] != "in place"]
        self.assertGreater(len(jumps), 500)
        for item in jumps:
            address, copy = int(item["address"], 16), int(item["copy"], 16)
            entry = hybrid_pe.read(address, 5)
            self.assertEqual(entry[0], 0xE9, item["key"])
            self.assertEqual(address + 5 + struct.unpack_from("<i", entry, 1)[0], copy, item["key"])
            self.assertGreaterEqual(copy, int(self.manifest["new_section"], 16))
        # (the rest of the original is untouched)
        self.assertEqual(hybrid_pe.read(0x401000, 0x100), pe.read(0x401000, 0x100))

    def test_exact_copies_are_the_originals_bytes(self):
        """An exact function that references nothing is copied byte for byte."""
        hybrid_pe = PEImage(self.image)
        entries = {f"{f['source']}:{f['name']}": f for f in self.program.functions if "source" in f}
        checked = 0
        for item in self.manifest["injected"]:
            f = entries[item["key"]]
            coff = self.program.obj(f["source"])
            symbol = coff.defined(hybrid.function_symbol(f["name"]))
            section = coff.sections[symbol["section"] - 1]
            if section["relocs"] or item["copy"] == "in place":
                continue
            original = hybrid.PE(hybrid.ORIGINAL).read(int(f["address"], 0), f["size"])
            self.assertEqual(hybrid_pe.read(int(item["copy"], 16), f["size"]), original, item["key"])
            checked += 1
        self.assertGreater(checked, 20)

    @unittest.skipUnless(WIBO, "needs wibo")
    def test_exact_hybrid_compiles_the_same(self):
        """Injecting exact functions leaves the compiler's behaviour (its output, its messages) unchanged."""
        with tempfile.TemporaryDirectory() as work:
            work = Path(work)
            exe = work / "hybrid/mwcceppc.exe"
            hybrid.write(exe, self.image, self.manifest)
            (work / "test.c").write_text(PROGRAM)
            outputs = []
            for compiler in (hybrid.ORIGINAL, exe):
                obj = work / f"{len(outputs)}.o"
                done = subprocess.run([str(WIBO), str(compiler), "-nodefaults", "-proc", "gekko", "-O4,p", "-inline",
                                       "auto", "-lang=c", "-c", "test.c", "-o", str(obj)], cwd=work,
                                      capture_output=True, timeout=120)
                self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
                outputs.append((obj.read_bytes(), done.stdout, done.stderr))
            self.assertEqual(outputs[0], outputs[1])

    @unittest.skipUnless(WIBO, "needs wibo")
    def test_injected_code_runs(self):
        """A trap at the copy of memcpy crashes the compiler: the jumps reach the new section."""
        item = next(i for i in self.manifest["injected"] if i["key"] == "src/msl/mem.c:memcpy")
        image = bytearray(self.image)
        image[PEImage(self.image).offset(int(item["copy"], 16))] = 0xCC
        with tempfile.TemporaryDirectory() as work:
            work = Path(work)
            exe = work / "hybrid/mwcceppc.exe"
            hybrid.write(exe, bytes(image), self.manifest)
            (work / "test.c").write_text(PROGRAM)
            done = subprocess.run([str(WIBO), str(exe), "-c", "test.c", "-o", str(work / "test.o")], cwd=work,
                                  capture_output=True, timeout=120, preexec_fn=difftest.no_core)
            self.assertNotEqual(done.returncode, 0)


class PEImage(PEFile):
    """A hybrid's bytes, read by address (its new section included)."""

    def __init__(self, image):
        with tempfile.NamedTemporaryFile() as f:
            f.write(image)
            f.flush()
            super().__init__(f.name)

    def offset(self, address):
        return self.address_to_offset(address)


class DifftestTest(unittest.TestCase):
    def test_ninja_edges(self):
        text = ("cflags_base = -O4\nmw_version = GC/2.7\n"
                "build build/a.o: mwcc_sjis src/a$ b.c | dep || order\n"
                "  cflags = $cflags_base -pragma \"cats off\" $\n      -DX=1\n"
                "build build/b.o: mwcc src/b.c\n  mw_version = GC/1.3.2\n  cflags = -O2\n"
                "build build/c.o: mwcc_pch src/c.h\n")
        with tempfile.TemporaryDirectory() as work:
            (Path(work) / "build.ninja").write_text(text)
            units = difftest.corpus(work)
        self.assertEqual(len(units), 1)
        self.assertEqual(units[0]["source"], "src/a b.c")
        self.assertEqual(units[0]["flags"], ["-O4", "-pragma", "cats off", "-DX=1"])
        self.assertTrue(units[0]["sjis"])

    def test_failed_reference_is_not_a_match(self):
        failed = dict(status="error", sha1=None, rc=1, output="parse error")
        self.assertEqual(difftest.classify(failed, failed, ".", ".", "a.o")["result"], "reference_error")

    def test_corpus_key_tracks_source_header_and_compiler(self):
        with tempfile.TemporaryDirectory() as work:
            game = Path(work)
            (game / "include").mkdir()
            source = game / "a.c"
            header = game / "include/a.h"
            compiler = game / "retail.exe"
            source.write_text("int a;\n")
            header.write_text("#define N 1\n")
            compiler.write_bytes(b"retail")
            units = [dict(name="a.o", source="a.c", flags=["-i", "include"])]
            args = argparse.Namespace(game=game, sjiswrap=True, wibo="wibo")
            with mock.patch.object(hybrid, "ORIGINAL", compiler):
                key = difftest.corpus_key(units, args)
                self.assertEqual(key, difftest.corpus_key(units, args))
                source.write_text("int b;\n")
                source_key = difftest.corpus_key(units, args)
                self.assertNotEqual(key, source_key)
                header.write_text("#define N 2\n")
                header_key = difftest.corpus_key(units, args)
                self.assertNotEqual(source_key, header_key)
                compiler.write_bytes(b"changed")
                self.assertNotEqual(header_key, difftest.corpus_key(units, args))

    def test_corpus_key_tracks_runtime_and_enabled_sjis_wrapper(self):
        with tempfile.TemporaryDirectory() as work:
            game = Path(work)
            (game / "build/tools").mkdir(parents=True)
            compiler = game / "retail.exe"
            runtime = game / "wibo"
            wrapper = game / "build/tools/sjiswrap.exe"
            (game / "a.c").write_text("int a;\n")
            compiler.write_bytes(b"retail")
            runtime.write_bytes(b"runtime")
            wrapper.write_bytes(b"wrapper")
            units = [dict(name="a.o", source="a.c", flags=[], sjis=True)]
            args = argparse.Namespace(game=game, sjiswrap=True, wibo=runtime)
            with mock.patch.object(hybrid, "ORIGINAL", compiler):
                key = difftest.corpus_key(units, args)
                runtime.write_bytes(b"changed runtime")
                runtime_key = difftest.corpus_key(units, args)
                self.assertNotEqual(key, runtime_key)
                wrapper.write_bytes(b"changed wrapper")
                self.assertNotEqual(runtime_key, difftest.corpus_key(units, args))
                args.sjiswrap = False
                disabled_key = difftest.corpus_key(units, args)
                wrapper.write_bytes(b"unused wrapper")
                self.assertEqual(disabled_key, difftest.corpus_key(units, args))
                args.sjiswrap = True
                units[0]["sjis"] = False
                unwrapped_key = difftest.corpus_key(units, args)
                wrapper.write_bytes(b"also unused")
                self.assertEqual(unwrapped_key, difftest.corpus_key(units, args))

    def test_disk_usage_handles_concurrent_cleanup_without_following_links(self):
        with tempfile.TemporaryDirectory() as work:
            directory = Path(work)
            present = directory / "present"
            removed = directory / "removed"
            link = directory / "link"
            present.write_bytes(b"abc")
            removed.write_bytes(b"obsolete")
            link.symlink_to(present)

            def listed_paths(_):
                yield present
                yield link
                removed.unlink()
                yield removed

            with mock.patch.object(Path, "rglob", side_effect=listed_paths):
                self.assertEqual(difftest.disk_usage(directory), 3)

    def test_failed_retail_reference_is_rejected(self):
        units = [dict(name="a.o")]
        failure = {"a.o": dict(status="error", sha1=None, rc=1, output="parse error")}
        with tempfile.TemporaryDirectory() as work:
            state = Path(work)
            with mock.patch.object(difftest, "STATE", state), \
                    mock.patch.object(difftest, "corpus_key", return_value="test"), \
                    mock.patch.object(difftest, "compile_all", return_value=failure):
                (state / "reference-test").mkdir()
                with self.assertRaisesRegex(RuntimeError, "retail reference failed"):
                    difftest.reference(argparse.Namespace(), units)

    def test_section_diff(self):
        a = elf({".text": b"\x01\x02\x03\x04", ".data": b"ab"})
        b = elf({".text": b"\x01\x02\x09\x04", ".data": b"ab"})
        self.assertEqual(difftest.section_diff(a, a), ([], None))
        sections, first = difftest.section_diff(a, b)
        self.assertEqual(sections, [".text"])
        self.assertTrue(first.startswith(".text+0x2"))


def elf(sections):
    """A minimal ELF32 big-endian object with SECTIONS (and its section name table)."""
    names = b"\0" + b"".join(n.encode() + b"\0" for n in sections) + b".shstrtab\0"
    bodies = [*sections.values(), names]
    offset, data = 0x34, bytearray()
    headers = [bytes(40)]
    name_offset = 1
    for i, (name, body) in enumerate([*sections.items(), (".shstrtab", names)]):
        headers.append(struct.pack(">IIIIIIIIII", name_offset, 3 if name == ".shstrtab" else 1, 0, 0,
                                   offset + len(data), len(body), 0, 0, 1, 0))
        name_offset += len(name) + 1
        data += body
    shoff = 0x34 + len(data)
    header = bytearray(b"\x7fELF\x01\x02\x01" + bytes(9))
    header += struct.pack(">HHIIIIIHHHHHH", 1, 20, 1, 0, 0, shoff, 0, 0x34, 0, 0, 40, len(headers),
                          len(headers) - 1)
    return bytes(header + data + b"".join(headers))


class ValidationSafetyTest(unittest.TestCase):
    def setUp(self):
        self.addCleanup(difftest.release_runs)

    def success(self, output=b"", stderr=b""):
        return dict(status="ok", sha1="same-object", rc=0, seconds=1.0,
                    output=(output + stderr).decode()[-4000:],
                    diagnostics_sha256=difftest.diagnostics_digest(output, stderr))

    def test_same_object_new_diagnostics_is_failure(self):
        comparison = difftest.classify(self.success(), self.success(stderr=b"new warning"), ".", ".", "a.o")
        self.assertEqual(comparison["result"], "messages_differ")
        self.assertTrue(comparison["objects_identical"])
        self.assertEqual(difftest.failing({"a.o": comparison}), ["a.o"])

    def test_diagnostic_prefix_outside_retained_tail_is_checked(self):
        tail = b"x" * 4000
        reference = self.success(b"original prefix" + tail)
        result = self.success(b"changed prefix" + tail)
        self.assertEqual(reference["output"], result["output"])
        self.assertEqual(difftest.classify(reference, result, ".", ".", "a.o")["result"], "messages_differ")

    def test_diagnostic_stream_boundary_is_checked(self):
        self.assertNotEqual(difftest.diagnostics_digest(b"ab", b"c"),
                            difftest.diagnostics_digest(b"a", b"bc"))

    def test_legacy_tail_only_equality_is_unverified(self):
        legacy = self.success()
        legacy.pop("diagnostics_sha256")
        result = difftest.classify(legacy, legacy, ".", ".", "a.o")
        self.assertEqual(result["result"], "diagnostics_unverified")
        self.assertEqual(difftest.failing({"a.o": result}), ["a.o"])

    def test_legacy_diagnostic_flag_is_not_hidden(self):
        comparison = {"a.o": dict(result="identical", messages_differ=True)}
        self.assertEqual(difftest.failing(comparison), ["a.o"])
        self.assertEqual(difftest.tally(comparison), {"messages_differ": 1})

    def test_new_run_does_not_clean_another_run(self):
        with tempfile.TemporaryDirectory() as work:
            state = Path(work)
            active = state / "another-live-run"
            active.mkdir()
            (active / "keep").write_text("evidence")
            args = argparse.Namespace(run="new-run", keep=0)
            with mock.patch.object(difftest, "STATE", state), mock.patch.object(difftest, "clean") as clean:
                difftest.new_run(args, "hybrid")
                clean.assert_not_called()
            self.assertEqual((active / "keep").read_text(), "evidence")

    def test_clean_preserves_live_and_untracked_runs(self):
        with tempfile.TemporaryDirectory() as work:
            state = Path(work)
            args = argparse.Namespace(run="active", keep=0)
            with mock.patch.object(difftest, "STATE", state):
                active = difftest.new_run(args, "hybrid")
                completed = state / "completed"
                completed.mkdir()
                (completed / ".run.lock").touch()
                legacy = state / "legacy-untracked"
                legacy.mkdir()
                difftest.clean(0)
                self.assertTrue(active.exists())
                self.assertTrue(legacy.exists())
                self.assertFalse(completed.exists())
                difftest.release_runs()
                difftest.clean(0)
                self.assertFalse(active.exists())
                self.assertTrue(legacy.exists())

    def test_explicit_run_collision_is_rejected(self):
        with tempfile.TemporaryDirectory() as work:
            args = argparse.Namespace(run="same", keep=1)
            with mock.patch.object(difftest, "STATE", Path(work)):
                difftest.new_run(args, "hybrid")
                with self.assertRaises(FileExistsError):
                    difftest.new_run(args, "hybrid")

    def test_automatic_run_names_are_unique(self):
        with tempfile.TemporaryDirectory() as work:
            args = argparse.Namespace(run=None, keep=1)
            with mock.patch.object(difftest, "STATE", Path(work)), mock.patch.object(difftest.time, "strftime", return_value="same-second"):
                first = difftest.new_run(args, "hybrid")
                second = difftest.new_run(args, "hybrid")
                self.assertNotEqual(first, second)

    def test_empty_hybrid_selection_never_compiles(self):
        with mock.patch.object(hybrid, "Program"), mock.patch.object(hybrid, "select", return_value=[]), \
                mock.patch.object(difftest, "new_run") as run:
            with self.assertRaisesRegex(SystemExit, "nothing selected"):
                difftest.command_hybrid(argparse.Namespace(), [])
            run.assert_not_called()

    def test_all_skipped_hybrid_never_compiles(self):
        with tempfile.TemporaryDirectory() as work:
            with mock.patch.object(difftest, "freeze_program"), mock.patch.object(difftest, "freeze_execution", side_effect=lambda args, units, run: args), mock.patch.object(hybrid, "Program"), mock.patch.object(hybrid, "select", return_value=["requested"]), \
                    mock.patch.object(difftest, "new_run", return_value=Path(work)), \
                    mock.patch.object(difftest, "build_hybrid", return_value={"injected": []}), \
                    mock.patch.object(difftest, "reference") as reference:
                with self.assertRaisesRegex(SystemExit, "no selected function"):
                    difftest.command_hybrid(argparse.Namespace(), [dict(name="a.o")])
                reference.assert_not_called()

    def test_hybrid_generator_rejects_zero_injections(self):
        with mock.patch.object(hybrid, "Program"), mock.patch.object(hybrid, "select", return_value=["requested"]), \
                mock.patch.object(hybrid, "build", return_value=(b"unused", {"injected": []})), \
                mock.patch.object(hybrid, "write") as write, mock.patch("sys.argv", ["hybrid.py", "out.exe"]):
            with self.assertRaisesRegex(SystemExit, "no selected function"):
                hybrid.main()
            write.assert_not_called()

    def test_main_propagates_validation_status(self):
        unit = dict(name="a.o")
        with mock.patch.dict("os.environ", {"DIFFTEST_SCOPED": "1"}), \
                mock.patch("sys.argv", ["difftest.py", "hybrid", "--function", "requested"]), \
                mock.patch.object(difftest, "corpus", return_value=[unit]), \
                mock.patch.object(difftest, "command_hybrid", return_value=1):
            self.assertEqual(difftest.main(), 1)

    def test_retail_and_hybrid_return_failure_status(self):
        units = [dict(name="a.o", output="game.o")]
        result = self.success()
        comparison = {"a.o": dict(result="messages_differ", messages_differ=True)}
        for command in [difftest.command_retail, difftest.command_hybrid]:
            with self.subTest(command=command.__name__), tempfile.TemporaryDirectory() as work:
                args = argparse.Namespace(game=Path(work))
                manifest = dict(injected=[{"key": "requested"}], skipped={}, sha1="hybrid")
                with mock.patch.object(difftest, "new_run", return_value=Path(work)), \
                        mock.patch.object(difftest, "corpus_key", return_value="unused"), \
                        mock.patch.object(difftest, "freeze_program"), mock.patch.object(difftest, "freeze_execution", side_effect=lambda args, units, run: args), \
                        mock.patch.object(difftest, "reference", return_value=(Path(work), {"a.o": result})), \
                        mock.patch.object(difftest, "compile_all", return_value={"a.o": result}), \
                        mock.patch.object(difftest, "compare_runs", return_value=comparison), \
                        mock.patch.object(difftest, "keep_failing"), \
                        mock.patch.object(difftest, "write_summary", return_value=""), \
                        mock.patch.object(hybrid, "Program"), \
                        mock.patch.object(hybrid, "select", return_value=["requested"]), \
                        mock.patch.object(difftest, "build_hybrid", return_value=manifest):
                    self.assertEqual(command(args, units), 1)

    def test_empty_bisect_selection_never_compiles_reference(self):
        with mock.patch.object(hybrid, "Program"), mock.patch.object(hybrid, "select", return_value=[]), \
                mock.patch.object(difftest, "Bisector") as bisector:
            with self.assertRaisesRegex(SystemExit, "nothing selected"):
                difftest.command_bisect(argparse.Namespace(), [])
            bisector.assert_not_called()

    def test_run_names_cannot_escape_or_overwrite_references(self):
        with tempfile.TemporaryDirectory() as work, mock.patch.object(difftest, "STATE", Path(work) / "state"):
            for name in ["", ".", "..", "../outside", str(Path(work) / "outside"), "reference-cache"]:
                with self.subTest(name=name), self.assertRaises(ValueError):
                    difftest.new_run(argparse.Namespace(run=name), "hybrid")
            self.assertFalse((Path(work) / "state").exists())

    def test_cleanup_does_not_follow_run_symlinks(self):
        with tempfile.TemporaryDirectory() as work:
            state, outside = Path(work) / "state", Path(work) / "outside"
            state.mkdir(); outside.mkdir()
            (outside / ".run.lock").touch()
            (state / "linked").symlink_to(outside, target_is_directory=True)
            with mock.patch.object(difftest, "STATE", state):
                difftest.clean(0)
            self.assertTrue(outside.exists())
            self.assertTrue((state / "linked").is_symlink())


class FreezeTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name) / 'repo'
        self.run = Path(self.tmp.name) / 'run'
        self.run.mkdir()
        self.config = self.root / 'config/GC_2_7'
        self.config.mkdir(parents=True)
        self.functions = [dict(name='A', source='src/A.c', address='0x400000', size=10),
                          dict(name='B', source='src/B.c', address='0x400010', size=10),
                          dict(name='Missing', source='src/Missing.c', address='0x400020', size=10)]
        (self.config / 'functions.json').write_text(json.dumps(self.functions))
        (self.config / 'bindings.json').write_text('{"_G":"0x500000"}')
        for name in ('A', 'B'):
            p = self.root / f'build/GC_2_7/compiled/src/{name}.c.obj'
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(name.encode())
        self.exe = self.root / 'build/compilers/GC/2.7/mwcceppc.exe'
        self.exe.parent.mkdir(parents=True)
        self.exe.write_bytes(b'retail')
        (self.exe.parent / 'lmgr326b.dll').write_bytes(b'dll')
        self.addCleanup(mock.patch.stopall)
        mock.patch.object(hybrid, 'ORIGINAL', self.exe).start()
        self.program = hybrid.Program(self.root)

    def test_all_configured_supplier_objects_and_missing_ledger(self):
        frozen = difftest.freeze_program(self.program, self.run)
        self.assertEqual((frozen.root / 'build/GC_2_7/compiled/src/B.c.obj').read_bytes(), b'B')
        self.assertIn('build/GC_2_7/compiled/src/Missing.c.obj', frozen.input_manifest['missing_objects'])
        self.assertEqual(json.loads((self.run / 'inputs.json').read_text()), frozen.input_manifest)

    def test_live_mutations_do_not_change_private_bytes(self):
        frozen = difftest.freeze_program(self.program, self.run)
        self.exe.write_bytes(b'poison')
        (self.exe.parent / 'lmgr326b.dll').write_bytes(b'poison')
        self.assertEqual(frozen.frozen_original.read_bytes(), b'retail')
        out = self.run / 'image/compiler.exe'
        hybrid.write(out, b'hybrid', {}, original=frozen.frozen_original)
        self.assertEqual((out.parent / 'lmgr326b.dll').read_bytes(), b'dll')
        self.assertFalse((out.parent / 'lmgr326b.dll').is_symlink())

    def test_changed_config_since_selection_rejected(self):
        (self.config / 'bindings.json').write_text('{"_G":"0x600000"}')
        with self.assertRaisesRegex(RuntimeError, 'bindings changed'):
            difftest.freeze_program(self.program, self.run)
        self.assertFalse((self.run / 'inputs').exists())

    def test_mid_copy_content_drift_rejected(self):
        real = Path.write_bytes
        def write(path, data):
            value = real(path, data)
            if path.name == 'B.c.obj' and '.inputs-' in str(path):
                self.exe.write_bytes(b'changed')
            return value
        with mock.patch.object(Path, 'write_bytes', write):
            with self.assertRaisesRegex(RuntimeError, 'changed during freeze'):
                difftest.freeze_program(self.program, self.run)
        self.assertFalse((self.run / 'inputs.json').exists())

    def test_mid_copy_missing_supplier_appears_rejected(self):
        real = Path.write_bytes
        def write(path, data):
            value = real(path, data)
            if path.name == 'B.c.obj' and '.inputs-' in str(path):
                real(self.root / 'build/GC_2_7/compiled/src/Missing.c.obj', b'new')
            return value
        with mock.patch.object(Path, 'write_bytes', write):
            with self.assertRaisesRegex(RuntimeError, 'file set changed'):
                difftest.freeze_program(self.program, self.run)

    def test_durable_image_manifest_and_explicit_original(self):
        frozen = difftest.freeze_program(self.program, self.run)
        manifest = dict(injected=[dict(key='src/A.c:A')], skipped={})
        with mock.patch.object(hybrid, 'build', return_value=(b'image', manifest)) as build:
            difftest.build_hybrid(['src/A.c:A'], self.run / 'temporary/compiler.exe', frozen)
        self.assertEqual(build.call_args.kwargs['original'], frozen.frozen_original)
        self.assertEqual(len(list((self.run / 'images').glob('*.json'))), 1)
        saved = json.loads(next((self.run / 'images').glob('*.json')).read_text())
        self.assertEqual(saved['inputs_sha256'], frozen.input_manifest['sha256'])

    def test_duplicate_empty_and_missing_result_units_rejected(self):
        for units in ([], [dict(name='')], [dict(name='.')], [dict(name='a//b')],
                      [dict(name='a/./b')], [dict(name='a'), dict(name='a')]):
            with self.assertRaises(ValueError):
                difftest.validate_units(units)
        with self.assertRaisesRegex(RuntimeError, 'result keys'):
            difftest.compare_runs([dict(name='a')], {'a': {}}, {}, '.', '.')

    def test_bisect_skipped_group_rejected_before_compile(self):
        b = difftest.Bisector.__new__(difftest.Bisector)
        b.args = object()
        b.cache, b.tests, b.run, b.program = {}, 0, self.run, self.program
        with mock.patch.object(difftest, 'build_hybrid', return_value=dict(injected=[])), \
             mock.patch.object(difftest, 'compile_all') as compile_all:
            with self.assertRaisesRegex(RuntimeError, 'every requested key'):
                b.compare(['src/A.c:A'], ['unit'])
            compile_all.assert_not_called()



class RuntimeFreezeTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.game = self.root / 'game'
        self.run = self.root / 'run'
        self.run.mkdir()
        (self.game / 'src').mkdir(parents=True)
        (self.game / 'src/a.c').write_text('#include "local.h"')
        (self.game / 'src/local.h').write_text('local')
        (self.game / 'inc').mkdir()
        (self.game / 'inc/h.h').write_text('include')
        (self.game / 'build/tools').mkdir(parents=True)
        self.wrapper = self.game / 'build/tools/sjiswrap.exe'
        self.wrapper.write_bytes(b'wrapper')
        (self.wrapper.parent / 'wrapper.dll').write_bytes(b'dll')
        self.wibo = self.root / 'wibo'
        elf = bytearray(64)
        elf[:6] = b'\x7fELF\x02\x01'
        struct.pack_into('<HH', elf, 54, 56, 0)
        self.wibo.write_bytes(elf)
        self.original = self.root / 'retail.exe'
        self.original.write_bytes(b'retail')
        self.args = SimpleNamespace(game=self.game, wibo=self.wibo, sjiswrap=True)
        self.units = [dict(name='a.o', source='src/a.c', flags=['-Iinc'], sjis=True)]
        self.addCleanup(mock.patch.stopall)
        mock.patch.object(hybrid, 'ORIGINAL', self.original).start()
        mock.patch.object(difftest, 'STATE', self.root / 'state').start()

    def test_runtime_private_paths_and_dll_copies_preserve_cwd(self):
        args = difftest.freeze_execution(self.args, self.units, self.run)
        self.assertEqual(args.game, self.game)
        self.assertEqual((self.run / 'runtime/wrapper/wrapper.dll').read_bytes(), b'dll')
        self.assertEqual(Path(args.sjiswrap_path).read_bytes(), b'wrapper')
        self.assertNotEqual(args.wibo, self.wibo)
        self.assertFalse(Path(args.sjiswrap_path).is_symlink())
        self.assertEqual(self.args.wibo, self.wibo)
        with mock.patch.object(difftest.subprocess, 'run', return_value=SimpleNamespace(stdout=b'',stderr=b'',returncode=1)) as run:
            difftest.compile_unit(self.original, self.units[0], self.run / 'out', args, 1)
        self.assertEqual(run.call_args.kwargs['cwd'], self.game)
        self.assertEqual(run.call_args.args[0][0], str(args.wibo))
        self.assertEqual(run.call_args.args[0][1], str(args.sjiswrap_path))
        self.assertIn('src/a.c', run.call_args.args[0])

    def test_unchanged_source_content_fingerprint(self):
        args = difftest.freeze_execution(self.args, self.units, self.run)
        difftest.check_execution(args, final=True)
        metadata = json.loads((self.run / 'execution.json').read_text())
        self.assertTrue(metadata['observed_unchanged'])
        self.assertFalse(metadata['corpus_fully_frozen'])
        self.assertIn('producer-stability-required', metadata['consistency'])

    def test_local_header_drift_rejected(self):
        args = difftest.freeze_execution(self.args, self.units, self.run)
        (self.game / 'src/local.h').write_text('changed')
        with self.assertRaisesRegex(RuntimeError, 'drift'):
            difftest.check_execution(args, final=True)
        self.assertFalse(json.loads((self.run / 'execution.json').read_text())['observed_unchanged'])

    def test_attached_include_and_file_set_changes_detected(self):
        initial = difftest.corpus_key(self.units, self.args, self.original)
        (self.game / 'inc/new.h').write_text('new')
        self.assertNotEqual(initial, difftest.corpus_key(self.units, self.args, self.original))

    def test_live_runtime_drift_rejected_private_bytes_unchanged(self):
        args = difftest.freeze_execution(self.args, self.units, self.run)
        self.wrapper.write_bytes(b'changed')
        self.assertEqual(Path(args.sjiswrap_path).read_bytes(), b'wrapper')
        with self.assertRaisesRegex(RuntimeError, 'drift'):
            difftest.check_execution(args)

    def test_private_runtime_mutation_rejected(self):
        args = difftest.freeze_execution(self.args, self.units, self.run)
        Path(args.wibo).write_bytes(b'poison')
        with self.assertRaisesRegex(RuntimeError, 'private runtime'):
            difftest.check_execution(args)

    def test_disabled_wrapper_not_required(self):
        self.args.sjiswrap=False
        self.wrapper.unlink()
        args = difftest.freeze_execution(self.args, self.units, self.run)
        self.assertFalse(hasattr(args, 'sjiswrap_path'))
        self.assertEqual(set(args.execution_guard[-1]['runtime']), {'wibo'})

    def test_dynamic_loader_rejected_without_execution(self):
        elf=bytearray(120)
        elf[:6]=b'\x7fELF\x02\x01'
        struct.pack_into('<Q',elf,32,64)
        struct.pack_into('<HH',elf,54,56,1)
        struct.pack_into('<I',elf,64,3)
        self.wibo.write_bytes(elf)
        with self.assertRaisesRegex(ValueError, 'dynamic'):
            difftest.freeze_execution(self.args,self.units,self.run)
        self.assertFalse((self.run/'runtime').exists())

    def test_reference_records_require_artifacts_before_publication(self):
        directory=self.root/'state/reference-key'
        (directory/'objects').mkdir(parents=True)
        data={'a.o':dict(status='ok',sha1='0'*40,diagnostics_sha256='x',seconds=1)}
        with mock.patch.object(difftest,'corpus_key',return_value='key'),mock.patch.object(difftest,'compile_all',return_value=data):
            with self.assertRaisesRegex(RuntimeError,'inconsistent.*artifacts'):
                difftest.reference(self.args,self.units,self.original)
        self.assertFalse((directory/'results.json').exists())

    def test_bad_partial_json_fails_closed(self):
        directory=self.root/'state/reference-key'
        directory.mkdir(parents=True)
        (directory/'results.json').write_text('{"a.o":')
        with mock.patch.object(difftest,'corpus_key',return_value='key'):
            with self.assertRaises(json.JSONDecodeError):
                difftest.reference(self.args,self.units,self.original)

    def test_reference_publication_is_atomic_and_verified(self):
        directory=self.root/'state/reference-key'
        (directory/'objects').mkdir(parents=True)
        raw=b'object'; (directory/'objects/a.o').write_bytes(raw)
        data={'a.o':dict(status='ok',sha1=hashlib.sha1(raw).hexdigest(),diagnostics_sha256='x',seconds=1)}
        with mock.patch.object(difftest,'corpus_key',return_value='key'),mock.patch.object(difftest,'compile_all',return_value=data):
            difftest.reference(self.args,self.units,self.original)
        self.assertEqual(json.loads((directory/'results.json').read_text()),data)
        self.assertEqual(list(directory.glob('.reference-*')),[])

    def test_deleted_validated_artifact_preserves_recorded_reference_hash(self):
        directory=self.root/'state/reference-key'
        obj=directory/'objects/a.o'
        obj.parent.mkdir(parents=True)
        raw=b'object'; obj.write_bytes(raw)
        item=dict(status='ok',sha1=hashlib.sha1(raw).hexdigest(),
                  diagnostics_sha256='same',seconds=1,output='',rc=0)
        (directory/'results.json').write_text(json.dumps({'a.o':item}))
        real=Path.read_bytes
        def read(path):
            data=real(path)
            if path==obj:
                path.unlink()  # Model another writer after successful cached hash read.
            return data
        with mock.patch.object(difftest,'corpus_key',return_value='key'),mock.patch.object(Path,'read_bytes',read):
            refdir,data=difftest.reference(self.args,self.units,self.original)
        self.assertFalse(obj.exists())
        # Artifact loss alone does not invalidate a previously observed object digest.
        self.assertEqual(difftest.classify(data['a.o'],item,refdir,self.run,'a.o')['result'],'identical')

    def test_raw_zero_exit_without_object_is_crash_not_success(self):
        with mock.patch.object(difftest.subprocess, 'run', return_value=SimpleNamespace(stdout=b'',stderr=b'',returncode=0)):
            result=difftest.compile_unit(self.original,self.units[0],self.run/'missing',self.args,1)
        self.assertEqual(result['status'],'crash')
        self.assertIsNone(result['sha1'])
        self.assertIsNotNone(result['stdout_sha256'])
        self.assertEqual(difftest.classify(result,result,'.','.','a.o')['result'],'crash')

    def test_durable_full_trial_survives_temporary_directory_deletion(self):
        b=difftest.Bisector.__new__(difftest.Bisector)
        names=[f'unit{i}.o' for i in range(442)]
        keys=['src/A.c:A']
        b.args=object()
        b.units={name:dict(name=name) for name in names}
        b.run,b.program,b.cache,b.tests=self.run,object(),{},0
        b.ref_dir,b.ref,b.timeouts=self.run,{},{}
        result={name:dict(status='ok',sha1='object',stdout_sha256='stdout',stderr_sha256='stderr',
                          diagnostics_sha256='diagnostics') for name in names}
        comparisons={name:dict(result='identical') for name in names}
        comparisons[names[-1]]=dict(result='messages_differ',objects_identical=True)
        manifest=dict(injected=[dict(key=keys[0])],skipped={},sha256='image',inputs_sha256='inputs',
                      inputs_manifest=str(self.run/'inputs.json'))
        temporary=[]
        def compile_trial(exe,*args):
            temporary.append(Path(exe).parent)
            self.assertTrue(temporary[-1].is_dir())
            return result
        with mock.patch.object(difftest,'build_hybrid',return_value=manifest), \
             mock.patch.object(difftest,'compile_all',side_effect=compile_trial) as compile_all, \
             mock.patch.object(difftest,'compare_runs',return_value=comparisons):
            self.assertEqual(b.compare(keys,names),comparisons)
            self.assertEqual(b.compare(keys,names),comparisons)
            self.assertEqual(compile_all.call_count,1)
        self.assertFalse(temporary[0].exists())
        trial=json.loads(next((self.run/'trials').glob('*.json')).read_text())
        self.assertEqual(trial['requested'],keys)
        self.assertEqual(trial['units'],names)
        self.assertEqual(len(trial['comparison']),442)
        self.assertEqual(trial['results'],result)
        self.assertEqual(trial['comparison'][names[-1]]['result'],'messages_differ')
        self.assertTrue(Path(trial['manifest']['inputs_manifest']).is_absolute())

    def test_response_files_rejected_not_falsely_verified(self):
        self.units[0]['flags']=['@options.txt']
        with self.assertRaisesRegex(ValueError,'response-file'):
            difftest.corpus_key(self.units,self.args,self.original)


class EvidenceTest(unittest.TestCase):
    def test_nonmonotone_search_does_not_find_shortest_failing_prefix(self):
        b=difftest.Bisector.__new__(difftest.Bisector)
        b.fails=lambda keys,names: set(keys) in ({'a'},{'a','b','c','d'})
        group=b.minimal(['a','b','c','d'],['unit'])
        self.assertEqual(set(group),{'a','b','c','d'})
        self.assertTrue(b.fails(['a'],['unit']))

    def test_key_order_can_make_heuristic_return_green_group(self):
        b=difftest.Bisector.__new__(difftest.Bisector)
        b.fails=lambda keys,names: tuple(keys)==('a','b','c','d')
        group=b.minimal(['a','b','c','d'],['unit'])
        self.assertEqual(group,['d','c','b','a'])
        self.assertFalse(b.fails(group,['unit']))

    def test_trial_reference_hashes_complete_existing_evidence_without_compile(self):
        with tempfile.TemporaryDirectory() as work:
            b=difftest.Bisector.__new__(difftest.Bisector)
            b.run=Path(work)
            keys,names=['function'],['unit']
            cache=(hashlib.sha1('function'.encode()).hexdigest(),tuple(names))
            comparison={'unit':dict(result='messages_differ')}
            b.cache={cache:comparison}
            directory=b.run/'trials';directory.mkdir()
            identity=hashlib.sha256(json.dumps(cache).encode()).hexdigest()
            path=directory/(identity+'.json')
            data=dict(requested=keys,units=names,comparison=comparison,results={'unit':dict(stdout_sha256='stdout')},
                      manifest=dict(sha256='image',inputs_sha256='inputs'))
            raw=json.dumps(data).encode();path.write_bytes(raw)
            evidence=b.trial_reference(keys,names)
            self.assertEqual(evidence['sha256'],hashlib.sha256(raw).hexdigest())
            self.assertEqual(evidence['image_sha256'],'image')
            data['requested']=['wrong'];path.write_text(json.dumps(data))
            with self.assertRaisesRegex(RuntimeError,'does not match'):
                b.trial_reference(keys,names)

    def run_command_model(self, standalone_bad):
        tmp=tempfile.TemporaryDirectory();self.addCleanup(tmp.cleanup)
        run=Path(tmp.name)
        names=[f'unit{i}' for i in range(442)]
        units=[dict(name=n) for n in names]
        good={n:dict(result='identical') for n in names}
        bad=dict(good);bad[names[-1]]=dict(result='differs')
        group=['b','a']
        class Model:
            tests=3
            def __init__(self,*args):pass
            def compare(self,keys,which):
                actual=bad if keys==['a','b'] or (keys==group and standalone_bad) else good
                return {n:actual[n] for n in which}
            def fails(self,keys,which):return bool(keys) and bool(difftest.failing(self.compare(keys,which)))
            def sample(self,which):return which[:1]
            def minimal(self,keys,which):return group
            def note(self,text):pass
            def trial_reference(self,keys,which):
                return dict(path=str(run/'trials'/'evidence.json'),sha256='trial',image_sha256='image',
                            inputs_sha256='inputs',comparison_sha256='comparisons')
        program=SimpleNamespace(frozen_original=run/'retail')
        manifest=dict(injected=[dict(key='a'),dict(key='b')],skipped={})
        with mock.patch.object(hybrid,'Program',return_value=program), \
             mock.patch.object(hybrid,'select',return_value=['a','b']), \
             mock.patch.object(difftest,'new_run',return_value=run), \
             mock.patch.object(difftest,'freeze_execution',side_effect=lambda args,*a:args), \
             mock.patch.object(difftest,'freeze_program',return_value=program), \
             mock.patch.object(difftest,'build_hybrid',return_value=manifest), \
             mock.patch.object(difftest,'Bisector',Model):
            difftest.command_bisect(argparse.Namespace(),units)
        return json.loads((run/'results.json').read_text()),(run/'summary.txt').read_text(),(run/'exclude.txt').read_text()

    def test_green_alone_group_is_context_evidence_not_culprit(self):
        result,summary,excludes=self.run_command_model(False)
        self.assertEqual(result['culprits'],[])
        self.assertEqual(result['removed_groups'][0]['evidence'],'interaction_or_context_dependent')
        self.assertEqual(result['remaining_role'],'retail_control')
        self.assertFalse(result['source_selection_pass'])
        self.assertEqual(result['removed_groups'][0]['context']['selection'],['a','b'])
        self.assertEqual(excludes,'b\na\n')
        self.assertIn('retail_control',summary)

    def test_standalone_bad_has_full442_maps_and_trial_digests(self):
        result,summary,excludes=self.run_command_model(True)
        group=result['culprits'][0]
        self.assertEqual(group['evidence'],'standalone_failure')
        self.assertEqual(len(group['comparison']),442)
        self.assertEqual(len(result['initial_comparison']),442)
        self.assertEqual(len(result['comparison']),442)
        self.assertEqual(group['trial']['image_sha256'],'image')
        self.assertEqual(result['initial_trial']['inputs_sha256'],'inputs')
        self.assertEqual(result['remaining_trial']['comparison_sha256'],'comparisons')


if __name__ == "__main__":
    unittest.main()
