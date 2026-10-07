#!/usr/bin/env python3
"""Fail closed on lost objdiff instruction/argument matches.

Run after the coordinator's build: snapshot OUT.json VERSION, then
check BASELINE.json VERSION. Never builds. A percentage increase is insufficient.
Snapshots include all configured units, including presently unmapped coverage.
"""
import argparse
import collections
import hashlib
import json
import re
from pathlib import Path
import subprocess
import struct
import sys
import time

VERSIONS = ("GC_1_2_5", "GC_1_2_5n", "GC_1_3", "GC_2_7")
SCHEMA = 1


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":")).encode()).hexdigest()


def file_evidence(path):
    if not path.is_file():
        raise ValueError(f"missing input: {path}")
    return {"sha256": hashlib.sha256(path.read_bytes()).hexdigest(), "mtime_ns": path.stat().st_mtime_ns}


def section_digest(path):
    """Hash section bytes/layout, excluding COFF symbol spelling and timestamps."""
    data = path.read_bytes()
    if len(data) < 20:
        raise ValueError(f"truncated COFF: {path}")
    machine, count, _, _, _, optional, _ = struct.unpack_from("<HHIIIHH", data)
    if machine != 0x14C or optional:
        raise ValueError(f"unsupported COFF: {path}")
    sections = []
    for index in range(count):
        offset = 20 + index * 40
        name, _, _, size, pointer, _, _, _, _, flags = struct.unpack_from("<8sIIIIIIHHI", data, offset)
        payload = data[pointer:pointer + size] if pointer else b""
        if pointer and len(payload) != size:
            raise ValueError(f"truncated section: {path}")
        sections.append([name.hex(), size, flags, hashlib.sha256(payload).hexdigest()])
    return digest(sections)


def inventory_identities(inventory):
    """Retain independent compiled views when the original inventory aliases code."""
    counts = collections.Counter((int(f["address"], 0), f["size"]) for f in inventory)
    identities = {}
    for function in inventory:
        owner = str(Path(function["source"]).with_suffix("")) if "source" in function else "unrecovered"
        name = owner + "/" + function["name"]
        address = int(function["address"], 0)
        size = function["size"]
        identity = f"{address:08x}:{size}"
        if counts[address, size] > 1:
            identity += ":" + name
        identities[name] = identity
    return identities


def function_state(symbol, base_symbol):
    """Address keys belong to the immutable target, never to the rebuilt base."""
    left = symbol.get("instructions", [])
    right = base_symbol.get("instructions", []) if base_symbol else []
    if not left and int(symbol.get("size", 0)):
        raise ValueError(f"missing instruction coverage: {symbol['name']}")
    if base_symbol and len(left) != len(right):
        raise ValueError(f"unaligned diff rows: {symbol['name']}")
    rows = {}
    insertions = collections.Counter()
    previous = "START"
    next_addresses = [str(r["instruction"].get("address", "0")) if "instruction" in r else None for r in left]
    next_address = "END"
    for index in range(len(left) - 1, -1, -1):
        if next_addresses[index] is None:
            next_addresses[index] = next_address
        else:
            next_address = next_addresses[index]
    for index, row in enumerate(left):
        kind = row.get("diff_kind", "DIFF_NONE")
        if kind not in ("DIFF_NONE", "DIFF_ARG_MISMATCH", "DIFF_OP_MISMATCH", "DIFF_REPLACE", "DIFF_DELETE", "DIFF_INSERT"):
            raise ValueError(f"unknown diff kind: {kind}")
        instruction = row.get("instruction")
        if instruction is None:
            if not base_symbol or "instruction" not in right[index]:
                raise ValueError(f"missing insertion coverage: {symbol['name']} row {index}")
            inserted = dict(right[index]["instruction"])
            inserted.pop("address", None)
            key = json.dumps([previous, next_addresses[index], digest(inserted)], separators=(",", ":"))
            insertions[key] += 1
            continue
        address = str(instruction.get("address", "0"))
        if address in rows:
            raise ValueError(f"duplicate target address: {symbol['name']}:{address}")
        arg_count = sum("arg" in part for part in instruction.get("parts", []))
        full = base_symbol is not None and kind == "DIFF_NONE"
        opcode = base_symbol is not None and kind in ("DIFF_NONE", "DIFF_ARG_MISMATCH")
        if kind == "DIFF_ARG_MISMATCH" or row.get("arg_diff"):
            diffs = row.get("arg_diff", [])
            if len(diffs) != arg_count:
                raise ValueError(f"missing argument coverage: {symbol['name']}:{address}")
            args = [not bool(part) for part in diffs]
        else:
            args = [full] * arg_count
        rows[address] = {"kind": kind, "full": full, "opcode": opcode, "args": args}
        previous = address
    return {"mapped": base_symbol is not None, "rows": rows, "insertions": dict(insertions),
            "exact": bool(base_symbol) and all(r["full"] for r in rows.values()) and not insertions}


def diff_state(diff):
    if "left" not in diff or "right" not in diff:
        raise ValueError("objdiff output lacks target/base coverage")
    left, right = diff["left"], diff["right"]
    functions = {}
    for symbol in left.get("symbols", []):
        if symbol.get("kind") != "SYMBOL_FUNCTION":
            continue
        index = symbol.get("target_symbol")
        base = None
        if index is not None:
            symbols = right.get("symbols", [])
            if not isinstance(index, int) or not 0 <= index < len(symbols):
                raise ValueError(f"invalid symbol mapping: {symbol['name']}")
            base = symbols[index]
        # Unit identity is absolute original address+size; symbol identity inside
        # it is relative address+size, allowing justified recovered renames.
        key = f"{symbol.get('address', '0')}:{symbol.get('size', '0')}"
        if key in functions:
            raise ValueError(f"duplicate function identity: {key}")
        functions[key] = function_state(symbol, base)
    # Without a byte-level data schema, preservation is deliberately stricter:
    # any unit containing non-code bytes must retain its complete base object.
    data = [s for s in left.get("sections", []) if s.get("kind") != "SECTION_CODE" and int(s.get("size", 0))]
    return {"functions": functions, "data": digest(data) if data else None}


def capture(root, version):
    config_path = root / "objdiff.json"
    config = json.loads(config_path.read_text())
    inventory = json.loads((root / f"config/{version}/functions.json").read_text())
    identities = inventory_identities(inventory)
    cli = root / "build/tools/objdiff-cli"
    file_evidence(cli)
    headers = list((root / "include").rglob("*.h"))
    dependency_output = subprocess.run(["ninja", "-t", "deps"], cwd=root,
                                       check=True, capture_output=True, text=True).stdout
    dependencies = {}
    output = None
    for line in dependency_output.splitlines():
        match = re.match(r"^(.*): #deps \d+, deps mtime \d+ \((VALID|STALE)\)$", line)
        if match:
            output = match.group(1)
            dependencies[output] = {"valid": match.group(2) == "VALID", "paths": []}
        elif line.startswith("    ") and output:
            dependencies[output]["paths"].append(line.strip())
    units = {}
    sources = {}
    compiled = {}
    for unit in config["units"]:
        name = unit["name"]
        target_path = unit.get("target_path")
        if not target_path or not target_path.startswith(f"build/{version}/target/"):
            raise ValueError(f"configuration is not for {version}: {name}")
        target = file_evidence(root / target_path)
        metadata = unit.get("metadata", {})
        state = {"target": section_digest(root / target_path), "name": name,
                 "complete": bool(metadata.get("complete")),
                 "mapped": bool(unit.get("base_path")), "functions": {}, "data": None}
        if state["mapped"]:
            base_path = unit["base_path"]
            if not base_path.startswith(f"build/{version}/base/"):
                raise ValueError(f"base version mismatch: {name}")
            base = file_evidence(root / base_path)
            source_path = metadata.get("source_path")
            if not source_path:
                raise ValueError(f"mapped unit has no source freshness evidence: {name}")
            if source_path not in sources:
                sources[source_path] = file_evidence(root / source_path)
                object_relative = f"build/{version}/compiled/{source_path}.obj"
                object_path = root / object_relative
                compiled[source_path] = file_evidence(object_path)
                deps = dependencies.get(object_relative)
                if deps is None:
                    # Legacy pro4 rules have no depfile. Ninja's explicit-output
                    # dry run checks the rule's recorded prerequisites without
                    # executing commands or rebuilding anything.
                    dry_run = subprocess.run(["ninja", "-n", object_relative], cwd=root,
                                             check=True, capture_output=True, text=True)
                    if dry_run.stdout.strip() != "ninja: no work to do.":
                        raise ValueError(f"legacy object has pending prerequisites: {source_path}")
                    compiled[source_path]["dry_run"] = dry_run.stdout.strip()
                    deps = {"valid": True, "paths": [source_path]}
                if not deps["valid"]:
                    raise ValueError(f"missing/stale ninja dependency evidence: {source_path}")
                dependency_evidence = {p: file_evidence(root / p) for p in deps["paths"]}
                compiled[source_path]["dependencies"] = dependency_evidence
                newest_input = max([sources[source_path]["mtime_ns"]] +
                                   [p["mtime_ns"] for p in dependency_evidence.values()])
                if compiled[source_path]["mtime_ns"] < newest_input:
                    raise ValueError(f"stale compiled dependency inputs: {source_path}")
            if base["mtime_ns"] < compiled[source_path]["mtime_ns"]:
                raise ValueError(f"stale split base object: {name}")
            result = subprocess.run([str(cli), "diff", "-1", str(root / target_path),
                                     "-2", str(root / base_path), "-o", "-", "--format", "json"],
                                    check=True, capture_output=True, text=True)
            state.update(diff_state(json.loads(result.stdout)))
            state["base"] = section_digest(root / base_path)
        identity = identities.get(name)
        if identity is None:
            # Unknown code gaps and data sections encode the absolute address
            # in their name. Section length comes from the one-section COFF.
            if not name.startswith("unknown/"):
                raise ValueError(f"no original address identity: {name}")
            address = int(name.rsplit("/", 1)[1], 16)
            size = struct.unpack_from("<I", (root / target_path).read_bytes(), 36)[0]
            identity = f"{address:08x}:{size}"
        if identity in units:
            raise ValueError(f"duplicate original unit identity: {identity}")
        units[identity] = state
    if not units or not any(u["mapped"] for u in units.values()):
        raise ValueError("no mapped coverage available")
    return {"schema": SCHEMA, "version": version, "created_ns": time.time_ns(),
            "evidence": {"config": file_evidence(config_path), "cli": file_evidence(cli),
                         "sources": sources, "compiled": compiled,
                         "headers": {str(p.relative_to(root)): file_evidence(p) for p in headers}},
            "units": units}


def regressions(before, after):
    if before.get("schema") != SCHEMA or after.get("schema") != SCHEMA:
        raise ValueError("unavailable or incompatible row baseline")
    if before["version"] != after["version"]:
        raise ValueError("baseline version mismatch")
    errors = []
    for name, old in before["units"].items():
        new = after["units"].get(name)
        if new is None:
            errors.append(f"{name}: unit lost")
            continue
        if old["target"] != new["target"]:
            errors.append(f"{name}: original target changed")
            continue
        if old["complete"] and not new["complete"]:
            errors.append(f"{name}: linked/complete status lost")
        if old["mapped"] and not new["mapped"]:
            errors.append(f"{name}: base mapping lost")
        if old["data"] and old["data"] != new["data"]:
            errors.append(f"{name}: data coverage changed")
        if old["data"] and old.get("base") != new.get("base"):
            errors.append(f"{name}: non-code base bytes changed (strict data gate)")
        for function, previous in old["functions"].items():
            label = f"{name}:{function}"
            current = new["functions"].get(function)
            if current is None:
                errors.append(f"{label}: function lost")
                continue
            if previous["mapped"] and not current["mapped"]:
                errors.append(f"{label}: function mapping lost")
            if previous["exact"] and old.get("base") != new.get("base"):
                errors.append(f"{label}: exact base object bytes changed")
            for address, row in previous["rows"].items():
                now = current["rows"].get(address)
                if now is None:
                    errors.append(f"{label}@{address}: target row lost")
                    continue
                if row["full"] and not now["full"]:
                    errors.append(f"{label}@{address}: matched instruction lost")
                if row["opcode"] and not now["opcode"]:
                    errors.append(f"{label}@{address}: matched opcode lost")
                for index, matched in enumerate(row["args"]):
                    if matched and (index >= len(now["args"]) or not now["args"][index]):
                        errors.append(f"{label}@{address}: matched argument {index} lost")
            for anchor, count in current["insertions"].items():
                if count > previous["insertions"].get(anchor, 0):
                    errors.append(f"{label}: new/grown insertion at {anchor}")
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("snapshot", "check"))
    parser.add_argument("snapshot", type=Path)
    parser.add_argument("version", choices=VERSIONS)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    try:
        before = json.loads(args.snapshot.read_text()) if args.action == "check" else None
        if before is not None and (before.get("schema") != SCHEMA or before.get("version") != args.version):
            raise ValueError("baseline unavailable/incompatible with requested version")
        current = capture(args.root.resolve(), args.version)
        if args.action == "snapshot":
            args.snapshot.write_text(json.dumps(current, separators=(",", ":")) + "\n")
            print(f"snapshot {args.version}: {len(current['units'])} units -> {args.snapshot}")
            return 0
        errors = regressions(before, current)
        for error in errors:
            print(f"REGRESSION {error}")
        print(f"row gate: {'FAIL' if errors else 'PASS'} ({len(errors)} losses)")
        return bool(errors)
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as exc:
        print(f"row gate unavailable: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
