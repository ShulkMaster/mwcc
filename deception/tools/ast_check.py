#!/usr/bin/env python3
"""python deception/tools/ast_check.py {record,check} --game DIR [--jobs N] [--limit N]

Runs a compiler over a game's GC 2.7 translation units (its build.ninja, from its DTK configure) with the AST oracle,
one dump per unit in build/deception/ast/.
  record: the oracle (the original frontend) -> build/deception/ast/original/
  check:  the hybrid (reconstructed functions injected) -> build/deception/ast/hybrid/, compared with the original's
"""
import argparse
import difflib
import os
import re
import shlex
import subprocess
import zlib
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
VERSION = "GC/2.7"


def units(game):
    """(source, cflags, uses sjiswrap) of each unit the game builds with GC 2.7."""
    text = re.sub(r"\$\n\s*", " ", (game / "build.ninja").read_text())
    found = []
    for block in text.split("\nbuild ")[1:]:
        head = re.match(r"\s*(\S+):\s+(mwcc\w*)\s+(\S+)", block)
        if not head or f"mw_version = {VERSION}\n" not in block + "\n":
            continue
        cflags = re.search(r"\n +cflags = (.*)", block)
        found.append((head[3], re.sub(r"\s+", " ", cflags[1]).strip() if cflags else "", head[2] == "mwcc_sjis"))
    return found


def object_path(out_dir, source):
    # (a short object path: the compiler crashes on long ones, about 140 characters)
    return out_dir / "obj" / f"{zlib.crc32(source.encode()):08x}.o"


def read(path):
    return path.read_bytes() if path.exists() else b""


def hits_path(out_dir, source):
    return out_dir / "hits" / f"{zlib.crc32(source.encode()):08x}.txt"


def run(game, exe, unit, out_dir):
    source, cflags, sjis = unit
    dump = out_dir / (source.replace("/", "__") + ".ast")
    obj = object_path(out_dir, source)
    obj.parent.mkdir(parents=True, exist_ok=True)
    wibo = game / "build/tools/wibo"
    command = [str(wibo), *([str(game / "build/tools/sjiswrap.exe")] if sjis else []), str(exe),
               *shlex.split(cflags), "-c", source, "-o", str(obj)]
    for _ in range(2):
        # (MWCC under wibo now and then exits nonzero with no message: one retry)
        dump.unlink(missing_ok=True)
        hits = hits_path(out_dir, source)
        hits.parent.mkdir(parents=True, exist_ok=True)
        hits.unlink(missing_ok=True)
        result = subprocess.run(command, cwd=game, env={**os.environ, "MWCC_AST_DUMP": str(dump),
                                                        "MWCC_HITS": str(hits)},
                                capture_output=True, text=True, timeout=600)
        if result.returncode == 0:
            break
    return source, result.returncode, dump


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=["record", "check"])
    parser.add_argument("--game", type=Path, required=True, help="the game's decompilation (with build.ninja)")
    parser.add_argument("--jobs", type=int, default=os.cpu_count())
    parser.add_argument("--limit", type=int)
    args = parser.parse_args()
    game = args.game.resolve()
    exe = ROOT / ("build/deception/oracle.exe" if args.mode == "record" else "build/deception/hybrid.exe")
    out_dir = ROOT / "build/deception/ast" / ("original" if args.mode == "record" else "hybrid")
    out_dir.mkdir(parents=True, exist_ok=True)
    todo = units(game)[:args.limit]
    with ThreadPoolExecutor(args.jobs) as pool:
        results = list(pool.map(lambda unit: run(game, exe, unit, out_dir), todo))
    failed = [source for source, code, _ in results if code]
    functions = sum(dump.read_text(errors="replace").count("\nFUNCTION ") + dump.read_text(errors="replace")
                    .startswith("FUNCTION ") for _, code, dump in results if dump.exists())
    print(f"{len(results)} units, {functions} functions dumped, {len(failed)} compiles failed")
    for source in failed[:10]:
        print(f"  failed: {source}")
    if args.mode == "record":
        sys.exit(0)
    different, objects = [], []
    reference = ROOT / "build/deception/ast/original"
    for source, code, dump in results:
        a = read(reference / dump.name).decode(errors="replace").splitlines()
        b = read(dump).decode(errors="replace").splitlines()
        if a != b:
            different.append((source, a, b))
        elif read(object_path(reference, source)) != read(object_path(out_dir, source)):
            objects.append(source)
    print(f"{len(results) - len(different) - len(objects)} units identical to the original's (ASTs and objects), "
          f"{len(different)} with different ASTs, {len(objects)} with the same ASTs but different objects")
    for source in objects[:5]:
        print(f"  object differs: {source}")
    for source, a, b in different[:5]:
        print(f"--- {source}")
        for line in list(difflib.unified_diff(a, b, "original", "hybrid", n=2, lineterm=""))[:20]:
            print("   ", line)
    sys.exit(1 if different or objects or failed else 0)


if __name__ == "__main__":
    main()
