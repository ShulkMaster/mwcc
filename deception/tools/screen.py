#!/usr/bin/env python3
"""python deception/tools/screen.py --game DIR [--units N] [--parallel N] [FUNCTION ...]

Injects each reconstructed frontend function alone (all of config/GC_2_7/functions.json's frontend functions when
none is named) and compares the ASTs of N of the game's units with the original's (ast_check.py record first):
identical (ASTs and objects; the function ran), untested (it never ran), different (ASTs), object differs (same ASTs), crash (a compile failed) or
unlinked (a symbol without an address). Writes
build/deception/screen.json.
"""
import argparse
import json
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import ast_check

ROOT = Path(__file__).resolve().parents[2]


def screen(name, source, game, units, jobs):
    work = ROOT / "build/deception/screen" / name
    work.mkdir(parents=True, exist_ok=True)
    exe = work / "hybrid.exe"
    obj = ROOT / f"build/deception/compiled/deception/{source}.obj"
    link = subprocess.run([sys.executable, str(ROOT / "deception/tools/hybrid.py"), str(exe), "--hook",
                           str(ROOT / "build/deception/compiled/deception/tools/astdump.c.obj"),
                           "--inject", f"{obj}:{name}"], capture_output=True, text=True, cwd=ROOT)
    if link.returncode:
        return dict(result="unlinked", detail=link.stderr.strip().splitlines()[:3])
    implicit = [line.rsplit(": ", 1)[1] for line in link.stderr.splitlines() if line.startswith("copied")]
    different, crashed, objects, calls = [], [], [], 0
    reference = ROOT / "build/deception/ast/original"
    with ThreadPoolExecutor(jobs) as pool:
        for source_unit, code, dump in pool.map(lambda unit: ast_check.run(game, exe, unit, work), units):
            hits = ast_check.hits_path(work, source_unit)
            if hits.exists():
                calls += sum(int(line.split()[1]) for line in hits.read_text().splitlines())
            if code:
                crashed.append(source_unit)
            elif ast_check.read(dump) != ast_check.read(reference / dump.name):
                different.append(source_unit)
            elif ast_check.read(ast_check.object_path(work, source_unit)) != \
                    ast_check.read(ast_check.object_path(reference, source_unit)):
                objects.append(source_unit)
    result = ("crash" if crashed else "different" if different else "object differs" if objects
              else "identical" if calls else "untested")
    return dict(result=result, calls=calls, crashed=crashed[:5], different=different[:5], n_different=len(different),
                objects=objects[:5],
                implicit=[i for i in implicit if not i.startswith(("_dump_", "_put_", "_label_", "_indent"))])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("functions", nargs="*")
    parser.add_argument("--game", type=Path, required=True)
    parser.add_argument("--units", type=int, default=60)
    parser.add_argument("--parallel", type=int, default=4)
    args = parser.parse_args()
    game = args.game.resolve()
    every = ast_check.units(game)
    # (a spread of the game's units: every k-th)
    units = every[::max(1, len(every) // args.units)][:args.units]
    functions = json.loads((ROOT / "config/GC_2_7/functions.json").read_text())
    candidates = [(f["name"], f["source"]) for f in functions if f.get("source", "").startswith("src/frontend/")
                  and (not args.functions or f["name"] in args.functions)]
    jobs = max(1, (os.cpu_count() or 4) // args.parallel)
    out_path = ROOT / "build/deception/screen.json"
    results = json.loads(out_path.read_text()) if out_path.exists() and not args.functions else {}
    todo = [c for c in candidates if c[0] not in results]
    with ThreadPoolExecutor(args.parallel) as pool:
        for (name, source), result in zip(todo, pool.map(lambda c: screen(*c, game, units, jobs), todo)):
            results[name] = dict(source=source, **result)
            print(f"{result['result']:9} {name}", flush=True)
            if not args.functions:
                out_path.write_text(json.dumps(results, indent=1))
    if args.functions:
        for name in args.functions:
            print(json.dumps({name: results.get(name)}, indent=1))


if __name__ == "__main__":
    main()
