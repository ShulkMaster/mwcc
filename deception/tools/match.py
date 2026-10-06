#!/usr/bin/env python3
"""python deception/tools/match.py [--threshold PERCENT]

Each reconstructed frontend function against the original GC 2.7's: its code, relocations resolved to the original's
addresses (as tools/compare.py does), scored by objdiff (fuzzy match percent); combined with the behavior screen
(tools/screen.py) into a status:
  exact       the original's bytes
  equivalent  at least PERCENT (default 98) and identical behavior on the corpus (the function ran)
  near        at least PERCENT, behavior not yet verified (untested or not screened)
  divergent   different behavior: different ASTs or objects, or a crash
  nonmatching below PERCENT
  unlinked    not compiled from the reconstruction
A function with a relocation of no known address is scored with that field unresolved and listed as unbound (the
hybrid cannot inject it until the symbol is bound; --learn binds symbols from near matches).
Writes an objdiff project (build/deception/match/objdiff.json) and build/deception/match/status.json.
"""
import argparse
import json
import subprocess
import sys
from bisect import bisect_left
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import compare  # noqa: E402

VERSION = "GC_2_7"
MATCH = ROOT / "build/deception/match"


def addresses(functions):
    table = {}
    for path in (ROOT / f"config/{VERSION}/bindings.json", ROOT / "deception/config/bindings.json"):
        table.update({name: int(value, 0) for name, value in json.loads(path.read_text()).items()})
    for f in functions:
        table[f["name"] if f["name"].startswith("?") else "_" + f["name"]] = int(f["address"], 0)
    return table


def learn(functions, table, pe):
    """Addresses of unbound symbols from near matches: a relocation whose preceding instruction bytes are the
    original's at the same offset reads the symbol's address there. Kept only when every such site agrees."""
    import struct
    votes = {}
    objects = {}
    for f in functions:
        if not f.get("source", "").startswith("src/frontend/"):
            continue
        source = "deception/" + f["source"]
        obj = ROOT / f"build/deception/compiled/{source}.obj"
        if not obj.exists():
            continue
        if source not in objects:
            objects[source] = compare.read_object(obj)
        symbols, sections = objects[source]
        symbol = f["name"] if f["name"].startswith("?") else "_" + f["name"]
        try:
            section, begin, end = compare.function_section(symbols, sections, symbol)
        except ValueError:
            continue
        address, size = int(f["address"], 0), f["size"]
        original = pe.read(address, size)
        body = section["data"][begin:end]
        for offset, index, kind in section["relocs"]:
            local = offset - begin
            dest = symbols[index]
            if not begin <= offset < end or dest["section"] != 0 or local < 2 or local + 4 > min(len(body), size):
                continue
            name = dest["name"]
            if name in table or compare.c_symbol(name) in table:
                continue
            if body[local - 2:local] != original[local - 2:local]:
                continue
            addend = struct.unpack_from("<i", body, local)[0]
            raw = struct.unpack_from("<I", original, local)[0]
            if kind == 6:
                value = raw - addend
            elif kind == 20:
                value = raw + address + local + 4 - addend
            else:
                continue
            votes.setdefault(name, set()).add(value & 0xFFFFFFFF)
    learned = {name: next(iter(values)) for name, values in votes.items() if len(values) == 1}
    return learned, len(votes) - len(learned)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--threshold", type=float, default=98.0)
    parser.add_argument("--learn", action="store_true", help="add bindings learned from near matches to "
                        "deception/config/bindings.json")
    args = parser.parse_args()
    _, pe = compare.original(VERSION)
    fixups = compare.base_relocations(pe)
    functions = json.loads((ROOT / f"config/{VERSION}/functions.json").read_text())
    table = addresses(functions)
    if args.learn:
        learned, conflicting = learn(functions, table, pe)
        path = ROOT / "deception/config/bindings.json"
        bindings = json.loads(path.read_text())
        bindings.update({name: f"0x{value:08x}" for name, value in learned.items()})
        path.write_text(json.dumps(dict(sorted(bindings.items())), indent=2) + "\n")
        table.update(learned)
        print(f"learned {len(learned)} bindings ({conflicting} symbols with conflicting sites left out)")
    screen_path = ROOT / "build/deception/screen.json"
    screen = json.loads(screen_path.read_text()) if screen_path.exists() else {}
    objects, units, rows = {}, [], {}
    for f in functions:
        if not f.get("source", "").startswith("src/frontend/"):
            continue
        source = "deception/" + f["source"]
        name, address, size = f["name"], int(f["address"], 0), f["size"]
        symbol = name if name.startswith("?") else "_" + name
        unit = f"{Path(source).stem}/{name}"
        target = MATCH / "target" / f"{unit}.obj"
        compare.write_coff(target, pe.read(address, size), symbol)
        row = dict(source=source, address=f["address"], size=size)
        rows[name] = row
        objdiff_unit = dict(name=unit, target_path=str(target.relative_to(MATCH)),
                            metadata=dict(source_path=source, progress_categories=["code"]))
        units.append(objdiff_unit)
        obj = ROOT / f"build/deception/compiled/{source}.obj"
        if source not in objects:
            objects[source] = compare.read_object(obj) if obj.exists() else None
        if objects[source] is None:
            row["status"] = "unlinked"
            continue
        try:
            body, resolutions = compare.resolve_function(*objects[source], symbol, address, table, pe, size)
        except ValueError as error:
            # (scored with its relocation fields unresolved; not injectable until they are bound)
            row["unbound"] = str(error)
            symbols, sections = objects[source]
            try:
                section, begin, end = compare.function_section(symbols, sections, symbol)
            except ValueError:
                row["status"] = "unlinked"
                continue
            body, resolutions = section["data"][begin:end], []
        base = MATCH / "base" / f"{unit}.obj"
        compare.write_coff(base, body, symbol)
        objdiff_unit["base_path"] = str(base.relative_to(MATCH))
        row["exact"] = "unbound" not in row and body == pe.read(address, size) and (
            {address + r["offset"] for r in resolutions if r["kind"] == 6}
            == set(fixups[bisect_left(fixups, address):bisect_left(fixups, address + size)]))
    (MATCH / "objdiff.json").write_text(json.dumps({
        "$schema": "https://raw.githubusercontent.com/encounter/objdiff/main/config.schema.json",
        "min_version": "3.8.0", "build_target": False, "build_base": False,
        "units": units, "progress_categories": [{"id": "code", "name": "Code"}]}, indent=1) + "\n")
    subprocess.run([str(ROOT / "build/tools/objdiff-cli"), "report", "generate", "-p", str(MATCH), "-o",
                    str(MATCH / "report.json")], check=True)
    report = json.loads((MATCH / "report.json").read_text())
    for unit in report["units"]:
        name = unit["name"].split("/", 1)[1]
        for function in unit.get("functions", []):
            rows[name]["match"] = round(function.get("fuzzy_match_percent", 0.0), 2)
    for name, row in rows.items():
        if row.get("status") == "unlinked":
            continue
        behavior = screen.get(name, {}).get("result")
        row["behavior"] = behavior
        match = row.get("match", 0.0)
        if row.get("exact"):
            row["status"] = "exact"
        elif behavior in ("different", "object differs", "crash"):
            row["status"] = "divergent"
        elif match >= args.threshold:
            row["status"] = "equivalent" if behavior == "identical" else "near"
        else:
            row["status"] = "nonmatching"
    (MATCH / "status.json").write_text(json.dumps(rows, indent=1) + "\n")
    counts, sizes = Counter(), Counter()
    for row in rows.values():
        counts[row["status"]] += 1
        sizes[row["status"]] += row["size"]
    total = sum(sizes.values())
    print(f"{len(rows)} frontend functions with a 2.7 address ({total} bytes); threshold {args.threshold}%")
    for status in ("exact", "equivalent", "near", "divergent", "nonmatching", "unlinked"):
        print(f"  {status:12} {counts[status]:5} functions {sizes[status] / total:6.1%} of bytes")
    done = sizes["exact"] + sizes["equivalent"]
    print(f"done (exact or equivalent): {done / total:.1%} of mapped frontend bytes")
    unbound = [row for row in rows.values() if "unbound" in row]
    print(f"{len(unbound)} functions scored with unresolved relocations (not injectable yet)")


if __name__ == "__main__":
    main()
