#!/usr/bin/env python3
"""python tools/difftest.py COMMAND [options]

Differential test of the GC 2.7 compiler: compile a corpus with the original mwcceppc.exe and with a hybrid
(tools/hybrid.py) and compare the objects byte for byte. The corpus is every GC/2.7 compile of a game decompilation's
build.ninja (default ~/projects/mk/mk-deception), replayed under wibo with each unit's own flags; objects go to
~/.local/state/campaign/mwcc/difftest/RUN, never to the game's build.

Commands:
- corpus: list the units.
- retail: compile the corpus with the original twice and compare (determinism); the first pass becomes the reference
  the other commands compare against.
- hybrid SELECTION: build a hybrid (tools/hybrid.py's options: --exact, --all-sourced, --source, --function,
  --exclude, --exclude-file) and compare it with the reference.
- bisect SELECTION: heuristically remove groups from a failing selection, then replay each group alone over
  the whole corpus. Results distinguish observed standalone failures from context-dependent removals and
  retain the ordered selections, complete comparisons and trial hashes; minimality is not guaranteed.
- clean: explicitly delete completed runs while preserving active runs: keep the newest --keep, and the total under 10 GB.

Per unit: identical, differs (the sections, the first differing symbol), crash (no object, or a signal), error (the
compiler reported an error the original did not), timeout, messages_differ, or diagnostics_unverified.
Each run writes RUN/results.json and RUN/summary.txt.
The command runs itself in `systemd-run --user --scope -p MemoryMax=6G nice -n 10` with at most --jobs (4) compiles.
"""
import argparse
import copy
import fcntl
from contextlib import contextmanager
import hashlib
import json
import os
import resource
import re
import shlex
import shutil
import stat
import struct
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import hybrid

ROOT = hybrid.ROOT
MAIN = Path.home() / "projects/decomp/mwcc"
STATE = Path.home() / ".local/state/campaign/mwcc/difftest"
GAME = Path.home() / "projects/mk/mk-deception"
WIBO = GAME / "build/tools/wibo"
LIMIT = 10 << 30
SCOPE = ["systemd-run", "--user", "--scope", "--quiet", "-p", "MemoryMax=6G", "nice", "-n", "10"]


# Corpus

def ninja_edges(path):
    """The build edges of a build.ninja: dicts of rule, outputs, inputs and variables (edge over global)."""
    text = Path(path).read_text().replace("$\r\n", "$\n")
    lines, current = [], ""
    for line in text.split("\n"):
        if current:
            line = line.lstrip()
        if line.endswith("$") and not line.endswith("$$"):
            current += line[:-1]
            continue
        lines.append(current + line)
        current = ""
    globals_, edges, edge = {}, [], None

    def unescape(value, scope):
        out, i = [], 0
        while i < len(value):
            c = value[i]
            if c != "$" or i + 1 == len(value):
                out.append(c)
                i += 1
                continue
            n = value[i + 1]
            if n in " :$\n":
                out.append(n)
                i += 2
            elif n == "{":
                end = value.index("}", i)
                out.append(scope.get(value[i + 2:end], ""))
                i = end + 1
            else:
                j = i + 1
                while j < len(value) and (value[j].isalnum() or value[j] in "_-"):
                    j += 1
                out.append(scope.get(value[i + 1:j], ""))
                i = j
        return "".join(out)

    for line in lines:
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        if line[0] in " \t":
            if edge is not None and "=" in line:
                key, value = line.strip().split("=", 1)
                edge["vars"][key.strip()] = unescape(value.strip(), {**globals_, **edge["vars"]})
            continue
        edge = None
        if line.startswith("build "):
            # (paths: "$ " and "$:" escape a space and a colon, "$$" a dollar)
            words = line[6:].replace("$$", "\2").replace("$ ", "\0").replace("$:", "\1")
            head, tail = words.split(":", 1)
            tail = tail.split("||")[0].split("|")[0].split()

            def path(word):
                return word.replace("\0", " ").replace("\1", ":").replace("\2", "$")
            edge = dict(rule=tail[0], outputs=[path(w) for w in head.split()], inputs=[path(w) for w in tail[1:]],
                        vars={})
            edges.append(edge)
        elif "=" in line and not line.startswith(("rule ", "pool ", "default ", "include ", "subninja ")):
            key, value = line.split("=", 1)
            globals_[key.strip()] = unescape(value.strip(), globals_)
    for edge in edges:
        edge["vars"] = {**globals_, **edge["vars"]}
    return edges


def corpus(game=GAME, version="GC/2.7"):
    """The units: dicts of name, source, flags and whether the game's rule wraps the compiler in sjiswrap."""
    units = []
    for edge in ninja_edges(Path(game) / "build.ninja"):
        if not edge["rule"].startswith("mwcc") or "pch" in edge["rule"] or edge["vars"].get("mw_version") != version:
            continue
        output = edge["outputs"][0]
        units.append(dict(name=output.removeprefix("build/"), output=output, source=edge["inputs"][0],
                          flags=shlex.split(edge["vars"].get("cflags", "")), sjis="sjis" in edge["rule"]))
    return sorted(units, key=lambda u: u["name"])


def corpus_key(units, args, original=None):
    game = Path(args.game).resolve()
    inputs = {game / u["source"] for u in units}
    directories = set()
    local_directories = {p.parent for p in inputs}
    for directory in local_directories:
        inputs.update(p for p in directory.rglob("*") if p.is_file()
                      and p.suffix.lower() in (".h", ".hpp", ".hh", ".hxx", ".inc", ".inl", ".def", ".pch"))
    for unit in units:
        flags = unit["flags"]
        i = 0
        while i < len(flags):
            flag = flags[i]
            if flag.startswith("@"):
                raise ValueError("response-file dependencies are unsupported")
            if flag in ("-i", "-I", "-include", "-prefix"):
                if i + 1 == len(flags):
                    raise ValueError(f"missing dependency argument: {flag}")
                i += 1
                path = game / flags[i]
                if flag in ("-i", "-I"):
                    if not path.is_dir():
                        raise ValueError(f"declared include directory missing: {path}")
                    directories.add(path)
                else:
                    inputs.add(path)
                    directories.add(path.parent)
            elif flag.startswith("-I") and len(flag) > 2:
                path = game / flag[2:]
                if not path.is_dir():
                    raise ValueError(f"declared include directory missing: {path}")
                directories.add(path)
            i += 1
    for directory in sorted(directories):
        inputs.update(f for f in directory.rglob("*") if f.is_file())
    pending = list(inputs)
    visited = set()
    while pending:
        path = pending.pop()
        resolved = path.resolve()
        if resolved in visited:
            continue
        visited.add(resolved)
        for name in re.findall(r'^\s*#\s*include\s*"([^"\n]+)"', path.read_text(errors="replace"), re.MULTILINE):
            candidates = [path.parent / name, *(directory / name for directory in sorted(directories))]
            found = next((candidate for candidate in candidates if candidate.is_file()), None)
            if found is not None and found not in inputs:
                inputs.add(found)
                pending.append(found)
    digest = hashlib.sha256()
    digest.update(json.dumps([units, str(game), args.sjiswrap, str(args.wibo)], sort_keys=True).encode())
    runtime = {Path(args.wibo)} if Path(args.wibo).is_file() else set()
    if args.sjiswrap and any(unit.get("sjis", False) for unit in units):
        wrapper = Path(getattr(args, "sjiswrap_path", game / "build/tools/sjiswrap.exe"))
        runtime.add(wrapper)
        runtime.update(p for p in wrapper.parent.iterdir() if p.suffix.lower() == ".dll" and p.is_file())
    for path in sorted(inputs | {Path(original or hybrid.ORIGINAL)} | runtime):
        digest.update(str(path).encode() + b"\0")
        digest.update(hashlib.sha256(path.read_bytes()).digest())
    return digest.hexdigest()


# Compiling

def no_core():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def diagnostics_digest(stdout, stderr):
    return hashlib.sha256(struct.pack("<Q", len(stdout)) + stdout + stderr).hexdigest()


def compile_unit(exe, unit, out_dir, args, timeout):
    """One compile: dict(status, seconds, sha1, rc, output); the object stays at OUT_DIR/NAME."""
    obj = Path(out_dir) / unit["name"]
    obj.parent.mkdir(parents=True, exist_ok=True)
    obj.unlink(missing_ok=True)
    command = [str(args.wibo)]
    if args.sjiswrap and unit["sjis"]:
        command.append(str(getattr(args, "sjiswrap_path", Path(args.game) / "build/tools/sjiswrap.exe")))
    command += [str(exe), *unit["flags"], "-c", unit["source"], "-o", str(obj)]
    start = time.monotonic()
    try:
        done = subprocess.run(command, cwd=args.game, capture_output=True, timeout=timeout, preexec_fn=no_core)
    except subprocess.TimeoutExpired:
        return dict(status="timeout", seconds=round(time.monotonic() - start, 3), sha1=None, rc=None, output="")
    seconds = round(time.monotonic() - start, 3)
    output = (done.stdout + done.stderr).decode(errors="replace")
    sha1 = hashlib.sha1(obj.read_bytes()).hexdigest() if obj.exists() else None
    if done.returncode < 0 or done.returncode > 1 or (sha1 is None and done.returncode == 0) or "panicked" in output:
        status = "crash"
    elif done.returncode:
        status = "error"
    else:
        status = "ok"
    return dict(status=status, seconds=seconds, sha1=sha1, rc=done.returncode, output=output[-4000:],
                diagnostics_sha256=diagnostics_digest(done.stdout, done.stderr),
                stdout_sha256=hashlib.sha256(done.stdout).hexdigest(),
                stderr_sha256=hashlib.sha256(done.stderr).hexdigest())


def compile_all(exe, units, out_dir, args, timeouts=None):
    """{unit name: compile result} with --jobs compiles at a time."""
    def one(unit):
        timeout = (timeouts or {}).get(unit["name"], args.timeout)
        return unit["name"], compile_unit(exe, unit, out_dir, args, timeout)
    check_disk_limits()
    results = {}
    with ThreadPoolExecutor(args.jobs) as pool:
        for start in range(0, len(units), args.jobs):
            results.update(pool.map(one, units[start:start + args.jobs]))
            check_disk_limits(campaign=False)
    check_disk_limits()
    return results


# Comparing

def elf_sections(data):
    """{name: (header fields, bytes)} of an ELF32 big-endian object, and its symbols [(section, value, size,
    name)]."""
    if data[:4] != b"\x7fELF":
        return None, []
    shoff, = struct.unpack_from(">I", data, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from(">HHH", data, 0x2E)
    headers = [struct.unpack_from(">IIIIIIIIII", data, shoff + i * shentsize) for i in range(shnum)]

    def body(h):
        return b"" if h[1] == 8 else data[h[4]:h[4] + h[5]]
    names = body(headers[shstrndx])

    def name(offset, table):
        return table[offset:table.index(b"\0", offset)].decode(errors="replace")
    sections, seen = {}, {}
    for i, h in enumerate(headers):
        n = name(h[0], names) if i else ""
        seen[n] = seen.get(n, 0) + 1
        key = n if seen[n] == 1 else f"{n}#{seen[n]}"
        sections[key] = (h[1:4] + h[5:], body(h), i)
    symbols = []
    for h in headers:
        if h[1] == 2:  # SHT_SYMTAB
            strings = body(headers[h[6]])
            table = body(h)
            for j in range(0, len(table), 16):
                st_name, value, size, info, _, shndx = struct.unpack_from(">IIIBBH", table, j)
                if info & 0xF in (1, 2):  # OBJECT, FUNC
                    symbols.append((shndx, value, size, name(st_name, strings)))
    return sections, symbols


def section_diff(a, b):
    """The sections that differ between two objects, and for the first, the symbol at its first difference."""
    sa, syms = elf_sections(a)
    sb, _ = elf_sections(b)
    if sa is None or sb is None:
        return ["(not ELF)"], None
    differ = [n for n in dict.fromkeys([*sa, *sb]) if sa.get(n, (None, None))[:2] != sb.get(n, (None, None))[:2]]
    where = None
    for n in differ:
        if n in sa and n in sb and not n.startswith((".rela", ".symtab", ".strtab", ".shstrtab", ".comment")):
            x, y = sa[n][1], sb[n][1]
            offset = next((i for i in range(min(len(x), len(y))) if x[i] != y[i]), min(len(x), len(y)))
            index = sa[n][2]
            covering = [s for s in syms if s[0] == index and s[1] <= offset < s[1] + max(s[2], 1)]
            where = f"{n}+0x{offset:x}" + (f" ({covering[0][3]})" if covering else "")
            break
    return differ, where


def classify(reference, result, ref_dir, out_dir, name):
    """One unit's comparison: dict(result, ...)."""
    detail = result["output"].strip().splitlines()[-3:] + ([f"exit {result['rc']}"] if result["rc"] else [])
    if result["status"] in ("crash", "timeout"):
        return dict(result=result["status"], detail=detail)
    if reference["status"] != "ok" or reference["sha1"] is None:
        return dict(result="reference_error", detail=reference["output"].strip().splitlines()[-3:])
    if result["status"] != reference["status"]:
        return dict(result="error" if result["status"] == "error" else "differs", detail=detail)
    if result["sha1"] == reference["sha1"]:
        reference_digest = reference.get("diagnostics_sha256")
        result_digest = result.get("diagnostics_sha256")
        if reference_digest is not None and result_digest is not None:
            same_output = result_digest == reference_digest
        elif result["output"] != reference["output"]:
            same_output = False
        else:
            return dict(result="diagnostics_unverified", objects_identical=True)
        return dict(result="identical") if same_output else dict(
            result="messages_differ", objects_identical=True, messages_differ=True, detail=detail)
    if result["sha1"] is None or reference["sha1"] is None:
        return dict(result="differs", sections=["(no object)"])
    sections, where = section_diff((Path(ref_dir) / name).read_bytes(), (Path(out_dir) / name).read_bytes())
    return dict(result="differs", sections=sections, first=where)


def compare_runs(units, reference, results, ref_dir, out_dir):
    expected = validate_units(units)
    if set(results) != expected or not expected <= set(reference):
        raise RuntimeError("comparison result keys do not match corpus")
    return {u["name"]: classify(reference[u["name"]], results[u["name"]], ref_dir, out_dir, u["name"])
            for u in units}


def comparison_kind(comparison):
    if comparison["result"] == "identical" and comparison.get("messages_differ"):
        return "messages_differ"
    return comparison["result"]


def tally(comparison):
    counts = {}
    for c in comparison.values():
        kind = comparison_kind(c)
        counts[kind] = counts.get(kind, 0) + 1
    return counts


def failing(comparison):
    return sorted(n for n, c in comparison.items() if comparison_kind(c) != "identical")


# Runs

def disk_usage(path):
    total = 0
    for file in Path(path).rglob("*"):
        try:
            info = file.lstat()
        except FileNotFoundError:
            continue  # The campaign may remove a landed worktree during this scan.
        if stat.S_ISREG(info.st_mode):
            total += info.st_size
    return total


def check_disk_limits(campaign=True):
    if disk_usage(STATE) > LIMIT:
        raise RuntimeError("difftest exceeds its 10 GB disk limit")
    if campaign and disk_usage(STATE.parent) > 40 << 30:
        raise RuntimeError("campaign exceeds its 40 GB disk limit")


_RUN_LEASES = {}


@contextmanager
def state_lock():
    STATE.mkdir(parents=True, exist_ok=True)
    with (STATE / ".runs.lock").open("a+b") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        yield


def release_runs():
    for lock in _RUN_LEASES.values():
        lock.close()
    _RUN_LEASES.clear()


def clean(keep, protect=()):
    """Delete completed, leased runs; leave live, reference, protected and legacy untracked runs alone."""
    if not STATE.exists():
        return
    with state_lock():
        runs = sorted((p for p in STATE.iterdir() if p.is_dir() and not p.is_symlink() and not p.name.startswith("reference-")
                       and p not in protect and (p / ".run.lock").is_file()),
                      key=lambda p: p.stat().st_mtime, reverse=True)
        eligible, locks = [], []
        try:
            for run in runs:
                lock = (run / ".run.lock").open("a+b")
                try:
                    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
                except BlockingIOError:
                    lock.close()
                    continue
                locks.append(lock)
                eligible.append(run)
            for old in eligible[keep:]:
                shutil.rmtree(old)
            eligible = eligible[:keep]
            while eligible and disk_usage(STATE) > LIMIT:
                shutil.rmtree(eligible.pop())
        finally:
            for lock in locks:
                lock.close()


def new_run(args, kind):
    if args.run is not None and (not args.run or Path(args.run).name != args.run or args.run in (".", "..") or args.run.startswith("reference-")):
        raise ValueError("run must be a directory name, outside the reserved reference prefix")
    with state_lock():
        if args.run is None:
            run = Path(tempfile.mkdtemp(prefix=time.strftime("%Y%m%d-%H%M%S") + "-" + kind + "-", dir=STATE))
        else:
            run = STATE / args.run
            run.mkdir(parents=True, exist_ok=False)
        lock = (run / ".run.lock").open("a+b")
        fcntl.flock(lock, fcntl.LOCK_EX)
        _RUN_LEASES[run] = lock
    return run


def keep_failing(directory, names):
    """Delete the objects of identical units (the reference keeps them)."""
    names = set(names)
    for f in Path(directory).rglob("*"):
        if f.is_file() and str(f.relative_to(directory)) not in names:
            f.unlink()


def reference(args, units, original=None):
    """The original's compile of the corpus (directory, results), compiled when missing."""
    original = Path(original or hybrid.ORIGINAL)
    directory = STATE / f"reference-{corpus_key(units, args, original)}"
    results = directory / "results.json"
    data = json.loads(results.read_text()) if results.exists() else {}
    valid = set(data) == validate_units(units) and all(u["name"] in data and data[u["name"]]["status"] == "ok"
                and data[u["name"]]["sha1"] is not None
                and data[u["name"]].get("diagnostics_sha256") is not None
                and (directory / "objects" / u["name"]).is_file()
                and hashlib.sha1((directory / "objects" / u["name"]).read_bytes()).hexdigest()
                    == data[u["name"]]["sha1"] for u in units)
    if not valid:
        print(f"compiling the reference with the original: {directory}", flush=True)
        data = compile_all(original, units, directory / "objects", args)
        if set(data) != validate_units(units) or any(
                item.get("status") != "ok" or item.get("sha1") is None
                or item.get("diagnostics_sha256") is None
                or not (directory / "objects" / name).is_file()
                or hashlib.sha1((directory / "objects" / name).read_bytes()).hexdigest() != item["sha1"]
                for name, item in data.items()):
            raise RuntimeError("retail reference failed: inconsistent compilation artifacts")
        with tempfile.NamedTemporaryFile(mode="w", prefix=".reference-", dir=directory, delete=False) as temporary:
            temporary.write(json.dumps(data, indent=1, sort_keys=True) + "\n")
            temporary_path = Path(temporary.name)
        temporary_path.replace(results)
    if set(data) != validate_units(units):
        raise RuntimeError("reference result keys do not match corpus")
    failed = [name for name, item in data.items() if item["status"] != "ok" or item["sha1"] is None]
    if failed:
        raise RuntimeError(f"retail reference failed on {len(failed)} units; see {results}: {failed[:5]}")
    return directory / "objects", data


def timeouts(ref):
    """Per unit: ten times the original's time, at least 20 s (a hybrid may loop forever)."""
    return {name: max(20.0, 10 * r["seconds"]) for name, r in ref.items()}


def write_summary(run, title, units, comparison, extra=()):
    counts = tally(comparison)
    lines = [title, f"units: {len(units)}; " + ", ".join(f"{k} {v}" for k, v in sorted(counts.items())), *extra]
    for name in failing(comparison):
        c = comparison[name]
        detail = " ".join(c.get("sections", [])) + (f" first {c['first']}" if c.get("first") else "")
        if c.get("detail"):
            detail += " | " + " / ".join(c["detail"])
        lines.append(f"  {comparison_kind(c):9} {name} {detail}")
    text = "\n".join(lines) + "\n"
    (run / "summary.txt").write_text(text)
    return text


def command_retail(args, units):
    validate_units(units)
    run = new_run(args, "retail")
    args = freeze_execution(args, units, run)
    program = freeze_program(hybrid.Program(), run)
    STATE.joinpath(f"reference-{corpus_key(units, args, program.frozen_original)}").exists() and print("(reference already present)")
    ref_dir, ref = reference(args, units, program.frozen_original)
    again = compile_all(program.frozen_original, units, run / "objects", args)
    comparison = compare_runs(units, ref, again, ref_dir, run / "objects")
    keep_failing(run / "objects", failing(comparison))
    game = sum(1 for u in units if ref[u["name"]]["sha1"] and (Path(args.game) / u["output"]).exists()
               and hashlib.sha1((Path(args.game) / u["output"]).read_bytes()).hexdigest() == ref[u["name"]]["sha1"])
    statuses = tally({n: dict(result=r["status"]) for n, r in ref.items()})
    extra = [f"original statuses: {statuses}", f"same object as the game's own build: {game}/{len(units)}",
             f"compile time: {sum(r['seconds'] for r in ref.values()):.1f} s"]
    check_execution(args, final=True)
    (run / "results.json").write_text(json.dumps(dict(kind="retail", comparison=comparison, results=again), indent=1) + "\n")
    print(write_summary(run, f"retail vs retail ({run})", units, comparison, extra))
    return int(bool(failing(comparison)))


def validate_units(units):
    names = [u["name"] for u in units]
    if not names or any(not n for n in names) or len(names) != len(set(names)):
        raise ValueError("corpus names must be nonempty and unique")
    for name in names:
        if (Path(name).is_absolute() or any(part in ("", ".", "..") for part in name.split("/"))
                or "\\" in name or "\0" in name):
            raise ValueError("corpus name escapes output directory")
    return set(names)


def freeze_program(program, run):
    """Copy linker inputs privately; reject observed drift, without claiming an atomic source epoch."""
    run = Path(run)
    target = run / "inputs"
    original = Path(hybrid.ORIGINAL)
    sources = sorted({f["source"] for f in program.functions if "source" in f})
    for source in sources:
        if Path(source).is_absolute() or ".." in Path(source).parts or "\\" in source:
            raise ValueError("configured source escapes snapshot")
    configs = [Path(f"config/{hybrid.VERSION}/{n}.json") for n in ("functions", "bindings")]

    def inventory():
        paths = {str(p): program.root / p for p in configs}
        missing = []
        for source in sources:
            rel = f"build/{hybrid.VERSION}/compiled/{source}.obj"
            path = program.root / rel
            if path.is_file():
                paths[rel] = path
            else:
                missing.append(rel)
        paths["build/compilers/GC/2.7/mwcceppc.exe"] = original
        for dll in sorted(original.parent.iterdir()):
            if dll.suffix.lower() == ".dll" and dll.is_file():
                paths[f"build/compilers/GC/2.7/{dll.name}"] = dll
        return paths, missing

    paths, missing = inventory()
    initial = {name: path.read_bytes() for name, path in paths.items()}
    if json.loads(initial[str(configs[0])]) != program.functions:
        raise RuntimeError("functions changed since selection")
    bindings = {n: int(v, 0) for n, v in json.loads(initial[str(configs[1])]).items()}
    # Program stores derived addresses; preserve the originally loaded bindings separately.
    if bindings != program.input_bindings:
        raise RuntimeError("bindings changed since selection")
    records = {name: dict(size=len(data), sha256=hashlib.sha256(data).hexdigest())
               for name, data in initial.items()}
    with tempfile.TemporaryDirectory(prefix=".inputs-", dir=run) as staging:
        staging = Path(staging)
        for name, data in initial.items():
            path = staging / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        after_paths, after_missing = inventory()
        if set(paths) != set(after_paths) or missing != after_missing:
            raise RuntimeError("linker input file set changed during freeze")
        for name, path in after_paths.items():
            if hashlib.sha256(path.read_bytes()).hexdigest() != records[name]["sha256"]:
                raise RuntimeError(f"linker input changed during freeze: {name}")
        metadata = dict(schema=1, consistency="observed-drift-check; producer-stability-required",
                        files=records, missing_objects=missing)
        canonical = json.dumps(metadata, sort_keys=True, separators=(",", ":")).encode()
        metadata["sha256"] = hashlib.sha256(canonical).hexdigest()
        (staging / "inputs.json").write_text(json.dumps(metadata, indent=1, sort_keys=True) + "\n")
        for name in records:
            if hashlib.sha256((staging / name).read_bytes()).hexdigest() != records[name]["sha256"]:
                raise RuntimeError(f"private input copy differs: {name}")
        staging.rename(target)
    (run / "inputs.json").write_bytes((target / "inputs.json").read_bytes())
    frozen = hybrid.Program(target)
    frozen.frozen_original = target / "build/compilers/GC/2.7/mwcceppc.exe"
    frozen.input_manifest = metadata
    frozen.run = run
    return frozen


def static_elf(data):
    """Inspect ELF64 little-endian headers only; never execute a candidate loader."""
    if len(data) < 64 or data[:6] != b"\x7fELF\x02\x01":
        raise ValueError("runtime freeze requires a static ELF64 little-endian wibo")
    offset = struct.unpack_from("<Q", data, 32)[0]
    size, count = struct.unpack_from("<HH", data, 54)
    if size < 56 or offset + size * count > len(data):
        raise ValueError("invalid wibo program header table")
    for index in range(count):
        kind = struct.unpack_from("<I", data, offset + size * index)[0]
        if kind in (2, 3):  # PT_DYNAMIC / PT_INTERP
            raise ValueError("dynamic wibo runtime is unsupported by this private-copy slice")


def freeze_execution(args, units, run):
    """Freeze runtime bytes, retaining the original game cwd and logical source arguments."""
    live = copy.copy(args)
    original = Path(hybrid.ORIGINAL)
    start = corpus_key(units, live, original)
    paths = {"wibo": Path(args.wibo)}
    if args.sjiswrap and any(u.get("sjis", False) for u in units):
        wrapper = Path(args.game) / "build/tools/sjiswrap.exe"
        paths["wrapper/sjiswrap.exe"] = wrapper
        paths.update({"wrapper/" + p.name: p for p in wrapper.parent.iterdir()
                      if p.suffix.lower() == ".dll" and p.is_file()})
    before = {name: p.read_bytes() for name, p in paths.items()}
    static_elf(before["wibo"])
    target = Path(run) / "runtime"
    with tempfile.TemporaryDirectory(prefix=".runtime-", dir=run) as stage:
        stage = Path(stage)
        for name, data in before.items():
            path = stage / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            path.chmod(0o755 if name == "wibo" else 0o644)
        if corpus_key(units, live, original) != start:
            raise RuntimeError("corpus/runtime changed during execution freeze")
        for name, path in paths.items():
            if path.read_bytes() != before[name] or (stage / name).read_bytes() != before[name]:
                raise RuntimeError(f"runtime input changed during freeze: {name}")
        stage.rename(target)
    frozen = copy.copy(args)
    frozen.wibo = target / "wibo"
    if "wrapper/sjiswrap.exe" in paths:
        frozen.sjiswrap_path = target / "wrapper/sjiswrap.exe"
    metadata = dict(schema=1, corpus_sha256_start=start, corpus_fully_frozen=False,
                    consistency="observed-start/end-check; producer-stability-required",
                    scope="local header trees/literal quoted includes, declared -i/-I/attached-I, -prefix/-include; macro/implicit search/env unverified",
                    runtime={name: dict(source=str(paths[name]), size=len(data), sha256=hashlib.sha256(data).hexdigest())
                             for name, data in before.items()})
    frozen.execution_guard = (live, units, original, Path(run), metadata)
    (Path(run) / "execution.json").write_text(json.dumps(metadata, indent=1, sort_keys=True) + "\n")
    return frozen


def check_execution(args, final=False):
    guard = getattr(args, "execution_guard", None)
    if guard is None:
        return
    live, units, original, run, metadata = guard
    current = corpus_key(units, live, original)
    metadata["corpus_sha256_end"] = current
    metadata["observed_unchanged"] = current == metadata["corpus_sha256_start"]
    if final or not metadata["observed_unchanged"]:
        (run / "execution.json").write_text(json.dumps(metadata, indent=1, sort_keys=True) + "\n")
    if not metadata["observed_unchanged"]:
        raise RuntimeError("live corpus/runtime drift invalidates differential provenance")
    # Private runtime copies must also still equal recorded input bytes.
    for name, record in metadata["runtime"].items():
        if hashlib.sha256((run / "runtime" / name).read_bytes()).hexdigest() != record["sha256"]:
            metadata["private_runtime_unchanged"] = False
            (run / "execution.json").write_text(json.dumps(metadata, indent=1, sort_keys=True) + "\n")
            raise RuntimeError("private runtime bytes changed during execution")


def build_hybrid(keys, path, program):
    original = getattr(program, "frozen_original", hybrid.ORIGINAL)
    image, manifest = hybrid.build(keys, program, original=original)
    manifest["sha256"] = hashlib.sha256(image).hexdigest()
    manifest["requested"] = list(keys)
    if hasattr(program, "input_manifest"):
        manifest["inputs_sha256"] = program.input_manifest["sha256"]
        manifest["inputs_manifest"] = str(program.run / "inputs.json")
        if hasattr(program, "execution_inputs"):
            manifest["execution_manifest"] = str(program.run / "execution.json")
            manifest["corpus_sha256_start"] = program.execution_inputs["corpus_sha256_start"]
            manifest["runtime"] = program.execution_inputs["runtime"]
        durable = program.run / "images"
        durable.mkdir(exist_ok=True)
        (durable / (manifest["sha256"] + ".json")).write_text(
            json.dumps(manifest, indent=1, sort_keys=True) + "\n")
    hybrid.write(path, image, manifest, original=original)
    return manifest


def command_hybrid(args, units):
    program = hybrid.Program()
    keys = hybrid.select(program, args)
    if not keys:
        raise SystemExit("nothing selected")
    validate_units(units)
    run = new_run(args, "hybrid")
    args = freeze_execution(args, units, run)
    program = freeze_program(program, run)
    if hasattr(args, "execution_guard"):
        program.execution_inputs = args.execution_guard[-1]
    manifest = build_hybrid(keys, run / "hybrid/mwcceppc.exe", program)
    if not manifest["injected"]:
        raise SystemExit("no selected function could be injected")
    ref_dir, ref = reference(args, units, program.frozen_original)
    results = compile_all(run / "hybrid/mwcceppc.exe", units, run / "objects", args, timeouts(ref))
    comparison = compare_runs(units, ref, results, ref_dir, run / "objects")
    keep_failing(run / "objects", failing(comparison))
    check_execution(args, final=True)
    (run / "results.json").write_text(json.dumps(dict(
        kind="hybrid", argv=sys.argv[1:], comparison=comparison, results=results,
        partial_coverage=bool(manifest["skipped"]), manifest=manifest), indent=1) + "\n")
    extra = [f"hybrid: {len(manifest['injected'])} functions injected, {len(manifest['skipped'])} skipped "
             f"(see hybrid/mwcceppc.exe.json), sha1 {manifest['sha1']}"]
    print(write_summary(run, f"original vs hybrid {' '.join(sys.argv[2:])} ({run})", units, comparison, extra))
    return int(bool(failing(comparison)) or bool(manifest["skipped"]))


class Bisector:
    def __init__(self, args, units, run, program):
        self.args, self.units, self.run, self.program = args, {u["name"]: u for u in units}, run, program
        self.ref_dir, self.ref = reference(args, units, program.frozen_original)
        self.timeouts = timeouts(self.ref)
        self.cache = {}
        self.tests = 0
        self.log = (run / "bisect.log").open("a")

    def note(self, text):
        print(text, flush=True)
        self.log.write(text + "\n")
        self.log.flush()

    def compare(self, keys, names):
        """{unit: comparison} of a hybrid of KEYS over NAMES."""
        check_execution(self.args)
        cache_key = (hashlib.sha1("\n".join(keys).encode()).hexdigest(), tuple(names))
        if cache_key not in self.cache:
            self.tests += 1
            with tempfile.TemporaryDirectory(dir=self.run) as work:
                exe = Path(work) / "mwcceppc.exe"
                manifest = build_hybrid(keys, exe, self.program)
                if set(keys) != {item["key"] for item in manifest["injected"]}:
                    raise RuntimeError("bisect trial did not inject every requested key")
                units = [self.units[n] for n in names]
                results = compile_all(exe, units, Path(work) / "objects", self.args, self.timeouts)
                comparison = compare_runs(units, self.ref, results, self.ref_dir, Path(work) / "objects")
                trials = self.run / "trials"
                trials.mkdir(exist_ok=True)
                identity = hashlib.sha256(json.dumps(cache_key).encode()).hexdigest()
                (trials / (identity + ".json")).write_text(json.dumps(dict(
                    requested=list(keys), units=list(names), manifest=manifest,
                    comparison=comparison, results=results), indent=1, sort_keys=True) + "\n")
                self.cache[cache_key] = comparison
        check_execution(self.args)
        return self.cache[cache_key]

    def trial_reference(self, keys, names):
        """Reference existing durable evidence; never compile a new trial here."""
        cache_key = (hashlib.sha1("\n".join(keys).encode()).hexdigest(), tuple(names))
        identity = hashlib.sha256(json.dumps(cache_key).encode()).hexdigest()
        path = self.run / "trials" / (identity + ".json")
        raw = path.read_bytes()
        data = json.loads(raw)
        if (data["requested"] != list(keys) or data["units"] != list(names)
                or data["comparison"] != self.cache[cache_key]):
            raise RuntimeError("durable trial does not match cached comparison")
        manifest = data["manifest"]
        return dict(path=str(path), sha256=hashlib.sha256(raw).hexdigest(),
                    image_sha256=manifest["sha256"], inputs_sha256=manifest["inputs_sha256"],
                    comparison_sha256=hashlib.sha256(json.dumps(
                        data["comparison"], sort_keys=True, separators=(",", ":")).encode()).hexdigest())

    def fails(self, keys, names):
        return bool(keys) and bool(failing(self.compare(keys, names)))

    def minimal(self, keys, names):
        """Heuristic prefix search; healing interactions and key order can defeat minimality.
        Returned groups must be replayed alone before assigning standalone failure evidence."""
        fixed, candidates = [], list(keys)
        while True:
            low, high = 1, len(candidates)
            while low < high:
                middle = (low + high) // 2
                if self.fails(fixed + candidates[:middle], names):
                    high = middle
                else:
                    low = middle + 1
            fixed.append(candidates[low - 1])
            candidates = candidates[:low - 1]
            if self.fails(fixed, names) or not candidates:
                return fixed

    def sample(self, names):
        """Up to --sample failing units, the fastest to compile first."""
        return sorted(names, key=lambda n: (self.ref[n]["seconds"], n))[:self.args.sample]


def command_bisect(args, units):
    program = hybrid.Program()
    selected = hybrid.select(program, args)
    if not selected:
        raise SystemExit("nothing selected")
    validate_units(units)
    run = new_run(args, "bisect")
    args = freeze_execution(args, units, run)
    program = freeze_program(program, run)
    if hasattr(args, "execution_guard"):
        program.execution_inputs = args.execution_guard[-1]
    manifest = build_hybrid(selected, run / "initial/mwcceppc.exe", program)
    if not manifest["injected"]:
        raise SystemExit("no selected function could be injected")
    bisector = Bisector(args, units, run, program)
    remaining = [i["key"] for i in manifest["injected"]]
    bisector.note(f"bisecting {len(remaining)} functions over {len(units)} units ({run})")
    every = [u["name"] for u in units]
    culprits = []
    contexts = []
    initial_keys = list(remaining)
    full = bisector.compare(remaining, every)
    full_initial = full
    initial_trial = bisector.trial_reference(initial_keys, every)
    names = bisector.sample(failing(full))
    bisector.note(f"all: {tally(full)}")
    while names:
        if bisector.fails(remaining, names):
            context = dict(selection=list(remaining), sample=list(names),
                           trial=bisector.trial_reference(remaining, names))
            group = bisector.minimal(remaining, names)
            context["removed"] = list(group)
            contexts.append(context)
            culprits.append(group)
            remaining = [k for k in remaining if k not in group]
            bisector.note(f"removed group (standalone replay pending): {' + '.join(group)} ({bisector.tests} hybrids so far)")
            continue
        full = bisector.compare(remaining, every)
        bisector.note(f"after removing {len(culprits)} groups: {tally(full)}")
        names = bisector.sample(failing(full))
    # Replay removed groups alone to distinguish standalone from context-dependent observations.
    ranked = []
    for group, context in zip(culprits, contexts):
        alone = bisector.compare(group, every)
        bad = {n: alone[n] for n in failing(alone)}
        ranked.append(dict(functions=group, counts=tally(alone), failing=len(bad),
                           evidence="standalone_failure" if bad else "interaction_or_context_dependent",
                           standalone_failure_observed=bool(bad), context=context,
                           comparison=alone, trial=bisector.trial_reference(group, every),
                           examples={n: bad[n] for n in sorted(bad)[:5]}))
    ranked.sort(key=lambda r: (-r["failing"], r["functions"]))
    clean_full = bisector.compare(remaining, every)
    check_execution(args, final=True)
    remaining_role = "source_selection" if remaining else "retail_control"
    result = dict(schema=2, kind="bisect", argv=sys.argv[1:], injected=len(manifest["injected"]),
                  removed_groups=ranked,
                  culprits=[r for r in ranked if r["standalone_failure_observed"]],
                  initial_comparison=full_initial, initial_trial=initial_trial,
                  remaining=remaining, remaining_role=remaining_role,
                  source_selection_pass=bool(remaining) and not failing(clean_full),
                  remaining_counts=tally(clean_full), comparison=clean_full,
                  remaining_trial=bisector.trial_reference(remaining, every),
                  initial_manifest=manifest, hybrids=bisector.tests)
    (run / "results.json").write_text(json.dumps(result, indent=1) + "\n")
    (run / "exclude.txt").write_text("".join(f"{k}\n" for r in ranked for k in r["functions"]))
    lines = [f"bisect {' '.join(sys.argv[2:])} ({run}): {len(manifest['injected'])} functions, "
             f"{bisector.tests} hybrids",
             f"remaining {remaining_role} ({len(remaining)} functions): {tally(clean_full)}",
             "removed groups by observed standalone impact:"]
    for r in ranked:
        example = next(iter(r["examples"].items()), None)
        where = ""
        if example:
            c = example[1]
            where = f"e.g. {example[0]}: {c['result']} " + " ".join(c.get("sections", [])) + (
                f" first {c['first']}" if c.get("first") else "") + (
                " | " + " / ".join(c["detail"]) if c.get("detail") else "")
        lines.append(f"  {r['failing']:4} {r['evidence']} {' + '.join(r['functions'])} {r['counts']} {where}")
    text = "\n".join(lines) + "\n"
    (run / "summary.txt").write_text(text)
    print(text)


def main():
    p = hybrid.parser()
    p.description = __doc__.split("\n\n")[0]
    p.add_argument("command", choices=["corpus", "retail", "hybrid", "bisect", "clean"])
    p.add_argument("--game", type=Path, default=GAME)
    p.add_argument("--wibo", type=Path, help="default: the game's configured build/tools/wibo")
    p.add_argument("--sjiswrap", action=argparse.BooleanOptionalAction, default=True,
                   help="wrap as the game's rule does (default: enabled)")
    p.add_argument("--jobs", type=int, default=4)
    p.add_argument("--timeout", type=float, default=300)
    p.add_argument("--sample", type=int, default=8, help="bisect: failing units per test")
    p.add_argument("--run", help="the run's directory name")
    p.add_argument("--keep", type=int, default=12, help="runs kept")
    p.add_argument("--units", nargs="*", help="only these units (names as `corpus` lists them)")
    args = p.parse_args()
    if args.wibo is None:
        configured = args.game / "build/tools/wibo"
        args.wibo = configured if configured.exists() else MAIN / "build/tools/wibo"
    if args.jobs < 1 or args.keep < 0 or args.sample < 1 or args.timeout <= 0:
        p.error("jobs, sample and timeout must be positive; keep must be nonnegative")
    args.jobs = min(args.jobs, 4)
    if args.command not in ("corpus", "clean") and not os.environ.get("DIFFTEST_SCOPED"):
        os.environ["DIFFTEST_SCOPED"] = "1"
        os.execvp(SCOPE[0], [*SCOPE, sys.executable, *sys.argv])
    units = corpus(args.game)
    if args.units:
        units = [u for u in units if u["name"] in set(args.units)]
    if not units and args.command != "clean":
        p.error("no GC 2.7 compilation units selected")
    if args.command == "corpus":
        for u in units:
            print(u["name"], u["source"])
        print(f"{len(units)} units", file=sys.stderr)
    elif args.command == "clean":
        clean(args.keep)
    else:
        try:
            return {"retail": command_retail, "hybrid": command_hybrid, "bisect": command_bisect}[args.command](args, units)
        finally:
            release_runs()


if __name__ == "__main__":
    raise SystemExit(main())
