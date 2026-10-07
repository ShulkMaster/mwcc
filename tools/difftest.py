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
- bisect SELECTION: find the functions whose injection changes an object or breaks a compile: repeatedly the
  smallest prefix (in the original's address order) of the selection that fails on a sample of the failing units,
  removed until the rest is identical on the whole corpus; then each culprit alone over the whole corpus.
- clean: delete old runs (also done before each run): keep the newest --keep, and the total under 10 GB.

Per unit: identical, differs (the sections, the first differing symbol), crash (no object, or a signal), error (the
compiler reported an error the original did not), timeout. Each run writes RUN/results.json and RUN/summary.txt.
The command runs itself in `systemd-run --user --scope -p MemoryMax=6G nice -n 10` with at most --jobs (4) compiles.
"""
import argparse
import hashlib
import json
import os
import resource
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


def corpus_key(units, args):
    game = Path(args.game).resolve()
    inputs = {game / u["source"] for u in units}
    directories = set()
    for unit in units:
        flags = unit["flags"]
        for i, flag in enumerate(flags[:-1]):
            if flag in ("-i", "-I"):
                directory = game / flags[i + 1]
                if directory.is_dir():
                    directories.add(directory)
    for directory in sorted(directories):
        inputs.update(f for f in directory.rglob("*") if f.is_file())
    digest = hashlib.sha256()
    digest.update(json.dumps([units, str(game), args.sjiswrap, str(args.wibo)], sort_keys=True).encode())
    runtime = {Path(args.wibo)} if Path(args.wibo).is_file() else set()
    if args.sjiswrap and any(unit.get("sjis", False) for unit in units):
        runtime.add(game / "build/tools/sjiswrap.exe")
    for path in sorted(inputs | {hybrid.ORIGINAL} | runtime):
        digest.update(str(path).encode() + b"\0")
        digest.update(hashlib.sha256(path.read_bytes()).digest())
    return digest.hexdigest()[:20]


# Compiling

def no_core():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def compile_unit(exe, unit, out_dir, args, timeout):
    """One compile: dict(status, seconds, sha1, rc, output); the object stays at OUT_DIR/NAME."""
    obj = Path(out_dir) / unit["name"]
    obj.parent.mkdir(parents=True, exist_ok=True)
    obj.unlink(missing_ok=True)
    command = [str(args.wibo)]
    if args.sjiswrap and unit["sjis"]:
        command.append(str(Path(args.game) / "build/tools/sjiswrap.exe"))
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
    return dict(status=status, seconds=seconds, sha1=sha1, rc=done.returncode, output=output[-4000:])


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
        same_output = result["output"] == reference["output"]
        return dict(result="identical") if same_output else dict(result="identical", messages_differ=True)
    if result["sha1"] is None or reference["sha1"] is None:
        return dict(result="differs", sections=["(no object)"])
    sections, where = section_diff((Path(ref_dir) / name).read_bytes(), (Path(out_dir) / name).read_bytes())
    return dict(result="differs", sections=sections, first=where)


def compare_runs(units, reference, results, ref_dir, out_dir):
    return {u["name"]: classify(reference[u["name"]], results[u["name"]], ref_dir, out_dir, u["name"])
            for u in units}


def tally(comparison):
    counts = {}
    for c in comparison.values():
        counts[c["result"]] = counts.get(c["result"], 0) + 1
    return counts


def failing(comparison):
    return sorted(n for n, c in comparison.items() if c["result"] != "identical")


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


def clean(keep, protect=()):
    """Delete old runs: keep the newest KEEP and the total under LIMIT; never a reference or PROTECT."""
    if not STATE.exists():
        return
    runs = sorted((p for p in STATE.iterdir() if p.is_dir() and not p.name.startswith("reference-")
                   and p not in protect), key=lambda p: p.stat().st_mtime, reverse=True)
    for old in runs[keep:]:
        shutil.rmtree(old)
    runs = runs[:keep]
    while runs and disk_usage(STATE) > LIMIT:
        shutil.rmtree(runs.pop())


def new_run(args, kind):
    STATE.mkdir(parents=True, exist_ok=True)
    clean(args.keep)
    name = args.run or time.strftime("%Y%m%d-%H%M%S") + "-" + kind
    run = STATE / name
    run.mkdir(parents=True, exist_ok=True)
    return run


def keep_failing(directory, names):
    """Delete the objects of identical units (the reference keeps them)."""
    names = set(names)
    for f in Path(directory).rglob("*"):
        if f.is_file() and str(f.relative_to(directory)) not in names:
            f.unlink()


def reference(args, units):
    """The original's compile of the corpus (directory, results), compiled when missing."""
    directory = STATE / f"reference-{corpus_key(units, args)}"
    results = directory / "results.json"
    data = json.loads(results.read_text()) if results.exists() else {}
    valid = all(u["name"] in data and data[u["name"]]["status"] == "ok"
                and data[u["name"]]["sha1"] is not None
                and (directory / "objects" / u["name"]).is_file()
                and hashlib.sha1((directory / "objects" / u["name"]).read_bytes()).hexdigest()
                    == data[u["name"]]["sha1"] for u in units)
    if not valid:
        print(f"compiling the reference with the original: {directory}", flush=True)
        data = compile_all(hybrid.ORIGINAL, units, directory / "objects", args)
        results.write_text(json.dumps(data, indent=1, sort_keys=True) + "\n")
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
        lines.append(f"  {c['result']:9} {name} {detail}")
    text = "\n".join(lines) + "\n"
    (run / "summary.txt").write_text(text)
    return text


def command_retail(args, units):
    run = new_run(args, "retail")
    STATE.joinpath(f"reference-{corpus_key(units, args)}").exists() and print("(reference already present)")
    ref_dir, ref = reference(args, units)
    again = compile_all(hybrid.ORIGINAL, units, run / "objects", args)
    comparison = compare_runs(units, ref, again, ref_dir, run / "objects")
    keep_failing(run / "objects", failing(comparison))
    game = sum(1 for u in units if ref[u["name"]]["sha1"] and (Path(args.game) / u["output"]).exists()
               and hashlib.sha1((Path(args.game) / u["output"]).read_bytes()).hexdigest() == ref[u["name"]]["sha1"])
    statuses = tally({n: dict(result=r["status"]) for n, r in ref.items()})
    extra = [f"original statuses: {statuses}", f"same object as the game's own build: {game}/{len(units)}",
             f"compile time: {sum(r['seconds'] for r in ref.values()):.1f} s"]
    json.dump(dict(kind="retail", comparison=comparison, results=again), (run / "results.json").open("w"), indent=1)
    print(write_summary(run, f"retail vs retail ({run})", units, comparison, extra))


def build_hybrid(keys, path, program):
    image, manifest = hybrid.build(keys, program)
    hybrid.write(path, image, manifest)
    return manifest


def command_hybrid(args, units):
    program = hybrid.Program()
    keys = hybrid.select(program, args)
    run = new_run(args, "hybrid")
    manifest = build_hybrid(keys, run / "hybrid/mwcceppc.exe", program)
    ref_dir, ref = reference(args, units)
    results = compile_all(run / "hybrid/mwcceppc.exe", units, run / "objects", args, timeouts(ref))
    comparison = compare_runs(units, ref, results, ref_dir, run / "objects")
    keep_failing(run / "objects", failing(comparison))
    json.dump(dict(kind="hybrid", argv=sys.argv[1:], comparison=comparison, results=results),
              (run / "results.json").open("w"), indent=1)
    extra = [f"hybrid: {len(manifest['injected'])} functions injected, {len(manifest['skipped'])} skipped "
             f"(see hybrid/mwcceppc.exe.json), sha1 {manifest['sha1']}"]
    print(write_summary(run, f"original vs hybrid {' '.join(sys.argv[2:])} ({run})", units, comparison, extra))


class Bisector:
    def __init__(self, args, units, run, program):
        self.args, self.units, self.run, self.program = args, {u["name"]: u for u in units}, run, program
        self.ref_dir, self.ref = reference(args, units)
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
        cache_key = (hashlib.sha1("\n".join(keys).encode()).hexdigest(), tuple(names))
        if cache_key not in self.cache:
            self.tests += 1
            with tempfile.TemporaryDirectory(dir=self.run) as work:
                exe = Path(work) / "mwcceppc.exe"
                build_hybrid(keys, exe, self.program)
                units = [self.units[n] for n in names]
                results = compile_all(exe, units, Path(work) / "objects", self.args, self.timeouts)
                self.cache[cache_key] = compare_runs(units, self.ref, results, self.ref_dir, Path(work) / "objects")
        return self.cache[cache_key]

    def fails(self, keys, names):
        return bool(keys) and bool(failing(self.compare(keys, names)))

    def minimal(self, keys, names):
        """A small group of KEYS that fails on NAMES: the last function of the shortest failing prefix, then
        within the functions before it, with it fixed, until the group fails on its own."""
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
    run = new_run(args, "bisect")
    bisector = Bisector(args, units, run, program)
    selected = hybrid.select(program, args)
    # (only what links: a skipped function changes nothing)
    _, manifest = hybrid.build(selected, program)
    remaining = [i["key"] for i in manifest["injected"]]
    bisector.note(f"bisecting {len(remaining)} functions over {len(units)} units ({run})")
    every = [u["name"] for u in units]
    culprits = []
    full = bisector.compare(remaining, every)
    names = bisector.sample(failing(full))
    bisector.note(f"all: {tally(full)}")
    while names:
        if bisector.fails(remaining, names):
            group = bisector.minimal(remaining, names)
            culprits.append(group)
            remaining = [k for k in remaining if k not in group]
            bisector.note(f"culprit: {' + '.join(group)} ({bisector.tests} hybrids so far)")
            continue
        full = bisector.compare(remaining, every)
        bisector.note(f"without {len(culprits)} culprits: {tally(full)}")
        names = bisector.sample(failing(full))
    # (each culprit alone over the whole corpus: its impact)
    ranked = []
    for group in culprits:
        alone = bisector.compare(group, every)
        bad = {n: c for n, c in alone.items() if c["result"] != "identical"}
        ranked.append(dict(functions=group, counts=tally(alone), failing=len(bad),
                           examples={n: bad[n] for n in sorted(bad)[:5]}))
    ranked.sort(key=lambda r: (-r["failing"], r["functions"]))
    clean_full = bisector.compare(remaining, every)
    result = dict(kind="bisect", argv=sys.argv[1:], injected=len(manifest["injected"]), culprits=ranked,
                  remaining=remaining, remaining_counts=tally(clean_full), hybrids=bisector.tests)
    (run / "results.json").write_text(json.dumps(result, indent=1) + "\n")
    (run / "exclude.txt").write_text("".join(f"{k}\n" for r in ranked for k in r["functions"]))
    lines = [f"bisect {' '.join(sys.argv[2:])} ({run}): {len(manifest['injected'])} functions, "
             f"{bisector.tests} hybrids",
             f"without the culprits ({len(remaining)} functions): {tally(clean_full)}",
             "culprits by impact (units failing when injected alone):"]
    for r in ranked:
        example = next(iter(r["examples"].items()), None)
        where = ""
        if example:
            c = example[1]
            where = f"e.g. {example[0]}: {c['result']} " + " ".join(c.get("sections", [])) + (
                f" first {c['first']}" if c.get("first") else "") + (
                " | " + " / ".join(c["detail"]) if c.get("detail") else "")
        lines.append(f"  {r['failing']:4} {' + '.join(r['functions'])} {r['counts']} {where}")
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
        {"retail": command_retail, "hybrid": command_hybrid, "bisect": command_bisect}[args.command](args, units)


if __name__ == "__main__":
    main()
