#!/usr/bin/env python3
"""python tools/progress.py [VERSION ...] [--all]: decomp progress from build/<VERSION>/report.json.

Prints, per version, how much of the original is matched (byte-exact code), linked (in Matching sources) and
recovered as functions, overall and per subsystem, and writes build/<VERSION>/progress.json: the same numbers in the
shape progress sites read ({"code": ..., "code/total": ..., "functions": ...} per category).

Subsystems are the source directories (src/frontend, src/backend, ...); code no source claims yet counts as
"unattributed". With no VERSION, the version build.ninja was configured for; --all, every version with a report.
"""
import argparse
import json
import re
import sys
from pathlib import Path

VERSIONS = ["GC_1_2_5", "GC_1_2_5n", "GC_1_3", "GC_2_7"]
NAMES = {"frontend": "Frontend (lexer, preprocessor, parser)", "backend": "Backend (code generation)",
         "optimizer": "Optimizer", "driver": "Driver and plugin API", "runtime": "Runtime", "msl": "MSL",
         "unattributed": "Unattributed"}


def subsystem(unit):
    source = unit.get("metadata", {}).get("source_path")
    if not source:
        return "unattributed"
    parts = Path(source).parts
    return parts[1] if len(parts) > 2 and parts[0] == "src" else parts[0]


def tally(units):
    total = {"code": 0, "matched": 0, "linked": 0, "data": 0, "data_linked": 0, "functions": 0, "matched_functions": 0}
    for unit in units:
        m, code = unit.get("measures", {}), "code" in unit.get("metadata", {}).get("progress_categories", ["code"])
        size = int(m.get("total_code", 0)) if code else int(m.get("total_data", 0) or m.get("total_code", 0))
        if code:
            total["code"] += size
            total["matched"] += int(m.get("matched_code", 0))
            total["linked"] += int(m.get("complete_code", 0))
        else:
            total["data"] += size
            total["data_linked"] += size if unit.get("metadata", {}).get("complete") else 0
        total["functions"] += m.get("total_functions", 0)
        total["matched_functions"] += m.get("matched_functions", 0)
    return total


def percent(part, whole):
    return 100.0 * part / whole if whole else 0.0


def line(name, t):
    return (f"  {name:<40} {percent(t['matched'], t['code']):6.2f}% matched, {percent(t['linked'], t['code']):6.2f}% "
            f"linked ({t['matched']} / {t['code']} bytes); functions {t['matched_functions']} / {t['functions']} "
            f"({percent(t['matched_functions'], t['functions']):.2f}%)")


def progress(version):
    path = Path(f"build/{version}/report.json")
    if not path.exists():
        return None
    units = json.loads(path.read_text())["units"]
    groups = {}
    for unit in units:
        groups.setdefault(subsystem(unit), []).append(unit)
    everything = tally(units)
    print(f"{version} progress:")
    print(line("All", everything))
    for key in sorted(groups, key=lambda k: (k == "unattributed", k)):
        print(line(NAMES.get(key, key), tally(groups[key])))
    if everything["data"]:
        print(f"  {'Data':<40} {percent(everything['data_linked'], everything['data']):6.2f}% linked "
              f"({everything['data_linked']} / {everything['data']} bytes)")
    report = {}
    for key, t in [("all", everything), *((k, tally(v)) for k, v in sorted(groups.items()))]:
        report[key] = {"code": t["matched"], "code/total": t["code"], "code/linked": t["linked"],
                       "data": t["data_linked"], "data/total": t["data"],
                       "functions": t["matched_functions"], "functions/total": t["functions"]}
    Path(f"build/{version}/progress.json").write_text(json.dumps(report, indent=2) + "\n")
    return everything


def configured_version():
    match = re.search(r"^version = (\S+)$", Path("build.ninja").read_text(), re.M) if Path("build.ninja").exists() else None
    return match.group(1) if match else VERSIONS[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("versions", nargs="*", choices=[[]] + VERSIONS, default=[])
    parser.add_argument("--all", action="store_true")
    args = parser.parse_args()
    versions = VERSIONS if args.all else (args.versions or [configured_version()])
    found = [v for v in versions if progress(v) is not None]
    missing = sorted(set(versions) - set(found), key=VERSIONS.index)
    for version in missing:
        print(f"{version}: no build/{version}/report.json (run: python configure.py --version {version} && ninja progress)",
              file=sys.stderr)
    return 0 if found else 1


if __name__ == "__main__":
    sys.exit(main())
