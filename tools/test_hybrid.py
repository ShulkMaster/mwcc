"""Tests of tools/hybrid.py and tools/difftest.py (python tools/test_hybrid.py). The hybrid tests need a GC_2_7
build (build/GC_2_7/compiled); the compile tests also need wibo."""
import argparse
import hashlib
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
from pathlib import Path

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


if __name__ == "__main__":
    unittest.main()
