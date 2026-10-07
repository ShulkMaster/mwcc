#!/usr/bin/env python3
"""python tools/hybrid.py OUTPUT [--exact] [--all-sourced] [--source FILE ...] [--function NAME ...] [--exclude X ...]

A hybrid GC 2.7 compiler: the original build/compilers/GC/2.7/mwcceppc.exe with functions of the compiled sources
(build/GC_2_7/compiled, after a GC_2_7 build) linked into a new section, `.hybrid`. Each chosen function's original
entry jumps to its copy.

Selection (combined as a union, then --exclude removes):
- --exact: the functions tools/compare.py finds exact (the original's bytes; a check of the patcher itself)
- --all-sourced: every function of config/GC_2_7/functions.json with a source whose object emits it
- --source FILE: every function of FILE (src/... path)
- --function NAME: one function (its config/GC_2_7/functions.json name, or SOURCE:NAME)
- --exclude NAME|SOURCE|SOURCE:NAME, or --exclude-file FILE (one per line, # comments)

Linking: a reference resolves to the original's address (config/GC_2_7/functions.json, preferring the referencing
source's own function, then config/GC_2_7/bindings.json) unless it is a chosen function (its copy). Compiler
literals (@N), data with no address in the original and functions with no address in the original (a source's own
static helpers) are copied with the function that references them; the manifest (OUTPUT.json) lists the named ones.
A function too short for a jump (under 5 bytes) is patched in place when its copy fits and has no relocations, else
skipped. The output is deterministic: the same selection and objects give the same executable.
"""
import argparse
import hashlib
import json
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VERSION = "GC_2_7"
ORIGINAL = ROOT / "build/compilers/GC/2.7/mwcceppc.exe"
COMPILED = ROOT / f"build/{VERSION}/compiled"
LITERAL = re.compile(r"_?@\d+")


def c_symbol(name):
    """A Win32 stdcall name's C symbol (_Name@8 -> _Name)."""
    decorated = re.fullmatch(r"[_@]([A-Za-z_]\w*)@\d+", name)
    return "_" + decorated[1] if decorated else name


def function_symbol(name):
    """A functions.json name's COFF symbol (C++-mangled names carry no C underscore)."""
    return name if name.startswith("?") else "_" + name


class Unresolved(Exception):
    pass


class Coff:
    """A relocatable i386 COFF object (the compiled sources)."""

    def __init__(self, path, source=None):
        self.path = Path(path)
        self.source = source
        data = self.path.read_bytes()
        machine, count, _, symbols, nsym, optional, _ = struct.unpack_from("<HHIIIHH", data)
        if machine != 0x14C or optional:
            raise ValueError(f"{path}: not a relocatable i386 COFF")
        strings = data[symbols + nsym * 18:]

        def name(raw):
            if raw[:4] == b"\0\0\0\0":
                offset = struct.unpack_from("<I", raw, 4)[0]
                return strings[offset:strings.index(b"\0", offset)].decode()
            return raw.rstrip(b"\0").decode()

        self.sections = []
        for i in range(count):
            offset = 20 + i * 40
            size, raw, relocs, _, nrel, _, flags = struct.unpack_from("<IIIIHHI", data, offset + 16)
            self.sections.append(dict(
                name=name(data[offset:offset + 8]), flags=flags, code=bool(flags & 0x20),
                data=bytes(size) if flags & 0x80 or not raw else data[raw:raw + size].ljust(size, b"\0"),
                relocs=[struct.unpack_from("<IIH", data, relocs + j * 10) for j in range(nrel)]))
        self.symbols = {}
        i = 0
        while i < nsym:
            offset = symbols + i * 18
            value, section, kind, storage, aux = struct.unpack_from("<IhHBB", data, offset + 8)
            self.symbols[i] = dict(name=name(data[offset:offset + 8]), value=value, section=section, type=kind,
                                   storage=storage)
            i += 1 + aux

    def defined(self, symbol):
        """The defined function symbol for SYMBOL (exact, else its stdcall-decorated form), or None."""
        found = [s for s in self.symbols.values() if s["section"] > 0 and s["name"] == symbol]
        if not found:
            found = [s for s in self.symbols.values() if s["section"] > 0 and c_symbol(s["name"]) == symbol]
        found = [s for s in found if self.sections[s["section"] - 1]["code"]]
        return found[0] if len(found) == 1 else None

    def function_end(self, symbol):
        """The end of SYMBOL's function in its section: the next function or section-local label, else the end."""
        ends = [s["value"] for s in self.symbols.values() if s["section"] == symbol["section"]
                and s["value"] > symbol["value"] and (s["type"] & 0x20 or s["storage"] == 2)]
        return min(ends, default=len(self.sections[symbol["section"] - 1]["data"]))


class PE:
    """The original executable, patched in place, plus a new section."""

    def __init__(self, path):
        self.data = bytearray(Path(path).read_bytes())
        self.header = struct.unpack_from("<I", self.data, 0x3C)[0]
        coff = self.header + 4
        self.count = struct.unpack_from("<H", self.data, coff + 2)[0]
        self.optional = coff + 20
        self.table = self.optional + struct.unpack_from("<H", self.data, coff + 16)[0]
        self.image_base = struct.unpack_from("<I", self.data, self.optional + 28)[0]
        self.section_alignment, self.file_alignment = struct.unpack_from("<II", self.data, self.optional + 32)
        self.sections = []
        for i in range(self.count):
            offset = self.table + i * 40
            vsize, rva, size, raw = struct.unpack_from("<IIII", self.data, offset + 8)
            self.sections.append((rva, vsize, raw, size))
        last = max(self.sections)
        if len(self.data) != max(raw + size for _, _, raw, size in self.sections):
            raise SystemExit("the executable has data past its last section")
        if self.table + (self.count + 1) * 40 > min(raw for _, _, raw, _ in self.sections if raw):
            raise SystemExit("no room for another section header")
        self.new_rva = (last[0] + last[1] + self.section_alignment - 1) & -self.section_alignment
        self.new_base = self.image_base + self.new_rva
        self.new = bytearray()
        self.directory = self.optional + 96 + 5 * 8
        self.fixups = self.base_relocations()
        self.added_fixups = set()

    def base_relocations(self):
        """The addresses the loader fixes up (HIGHLOW entries of the base relocation table)."""
        rva, size = struct.unpack_from("<II", self.data, self.directory)
        offset = self.offset(self.image_base + rva)
        end, fixups = offset + size, set()
        while offset < end:
            page, block = struct.unpack_from("<II", self.data, offset)
            for i in range((block - 8) // 2):
                entry = struct.unpack_from("<H", self.data, offset + 8 + 2 * i)[0]
                if entry >> 12 == 3:
                    fixups.add(self.image_base + page + (entry & 0xFFF))
            offset += block
        return fixups

    def offset(self, address):
        rva = address - self.image_base
        for start, vsize, raw, size in self.sections:
            if start <= rva < start + min(vsize, size):
                return raw + rva - start
        raise ValueError(f"0x{address:08x} is not in the original's file")

    def read(self, address, size):
        offset = self.offset(address)
        return bytes(self.data[offset:offset + size])

    def write(self, address, data):
        """DATA over the original's bytes at ADDRESS; the loader no longer fixes up anything it overlaps."""
        offset = self.offset(address)
        self.data[offset:offset + len(data)] = data
        for site in range(address - 3, address + len(data)):
            self.fixups.discard(site)

    def place(self, data, align=16):
        """DATA's address in the new section."""
        while len(self.new) % align:
            self.new.append(0xCC)
        address = self.new_base + len(self.new)
        self.new += data
        return address

    def write_base_relocations(self):
        """A new base relocation table in the new section (wibo may map the image away from its preferred base)."""
        pages = {}
        for site in sorted(self.fixups | self.added_fixups):
            rva = site - self.image_base
            pages.setdefault(rva & ~0xFFF, []).append(0x3000 | (rva & 0xFFF))
        table = bytearray()
        for page, entries in sorted(pages.items()):
            if len(entries) % 2:
                entries.append(0)
            table += struct.pack("<II", page, 8 + 2 * len(entries)) + struct.pack(f"<{len(entries)}H", *entries)
        address = self.place(bytes(table), 4)
        struct.pack_into("<II", self.data, self.directory, address - self.image_base, len(table))

    def image(self):
        self.write_base_relocations()
        raw = (len(self.data) + self.file_alignment - 1) & -self.file_alignment
        size = (len(self.new) + self.file_alignment - 1) & -self.file_alignment
        header = struct.pack("<8sIIIIIIHHI", b".hybrid", len(self.new), self.new_rva, size, raw, 0, 0, 0, 0,
                             0xE0000020)
        self.data[self.table + self.count * 40:self.table + self.count * 40 + 40] = header
        struct.pack_into("<H", self.data, self.header + 6, self.count + 1)
        image = (self.new_rva + len(self.new) + self.section_alignment - 1) & -self.section_alignment
        struct.pack_into("<I", self.data, self.optional + 56, image)
        return bytes(self.data + bytes(raw - len(self.data)) + self.new + bytes(size - len(self.new)))


class Program:
    """The original's functions and bindings, and the compiled objects."""

    def __init__(self, root=ROOT):
        self.root = Path(root)
        self.functions = json.loads((self.root / f"config/{VERSION}/functions.json").read_text())
        self.addresses = {name: int(value, 0) for name, value in
                          json.loads((self.root / f"config/{VERSION}/bindings.json").read_text()).items()}
        self.own = {}  # (source, symbol) -> address
        for f in self.functions:
            symbol = function_symbol(f["name"])
            self.addresses.setdefault(symbol, int(f["address"], 0))
            if "source" in f:
                self.own[(f["source"], symbol)] = int(f["address"], 0)
        self.objects = {}
        self._globals = None

    def obj(self, source):
        """SOURCE's compiled object, or None when it was not built."""
        if source not in self.objects:
            path = self.root / f"build/{VERSION}/compiled/{source}.obj"
            self.objects[source] = Coff(path, source) if path.exists() else None
        return self.objects[source]

    def globals(self):
        """Every global symbol a compiled object defines: name -> (object, symbol)."""
        if self._globals is None:
            self._globals = {}
            for f in sorted({f["source"] for f in self.functions if "source" in f}):
                coff = self.obj(f)
                if coff is None:
                    continue
                for symbol in coff.symbols.values():
                    if symbol["section"] > 0 and symbol["storage"] == 2:
                        self._globals.setdefault(symbol["name"], (coff, symbol))
                        self._globals.setdefault(c_symbol(symbol["name"]), (coff, symbol))
        return self._globals

    def bound(self, source, name):
        """The original's address for NAME referenced from SOURCE, or None."""
        for candidate in dict.fromkeys((name, c_symbol(name))):
            if (source, candidate) in self.own:
                return self.own[(source, candidate)]
        for candidate in dict.fromkeys((name, c_symbol(name))):
            if candidate in self.addresses:
                return self.addresses[candidate]
        return None


def sourced(program):
    """Each function with a source, in the original's order: (key SOURCE:NAME, function entry)."""
    return [(f"{f['source']}:{f['name']}", f) for f in sorted(
        (f for f in program.functions if "source" in f), key=lambda f: int(f["address"], 0))]


def exact_functions(root=ROOT):
    """The keys of the functions tools/compare.py finds exact (the original's bytes and fixups)."""
    import os
    sys.path.insert(0, str(Path(root) / "tools"))
    import compare
    cwd = os.getcwd()
    os.chdir(root)
    try:
        rows, results = compare.check(VERSION)
    finally:
        os.chdir(cwd)
    return {f"{row['source']}:{row['name'].rsplit('/', 1)[1]}" for row in rows
            if "source" in row and results[row["name"]][1]}


class Linker:
    def __init__(self, program, pe):
        self.program = program
        self.pe = pe
        self._chosen = {}
        self.chosen_names = {}
        self.placed = {}  # (source, section index) -> address of its copy
        self.pending = []
        self.copied = {}  # name -> source: named functions and data copied with a chosen function

    @property
    def chosen(self):
        """(source, symbol) -> (coff, symbol entry): the injected functions."""
        return self._chosen

    @chosen.setter
    def chosen(self, value):
        self._chosen = dict(value)
        # (a reference from another source finds a chosen function by name: the first in the original's order)
        self.chosen_names = {}
        for (_, name), entry in self._chosen.items():
            self.chosen_names.setdefault(name, entry)

    def target(self, coff, symbol):
        """Where a reference from COFF to SYMBOL goes, as (kind, value): ("address", A), or ("chosen" for a chosen
        function, else "copy", (coff, section, offset)). Raises Unresolved."""
        name = symbol["name"]
        if symbol["section"] == -1:
            return "address", symbol["value"]
        if symbol["section"] == 0:
            for candidate in dict.fromkeys((name, c_symbol(name))):
                if candidate in self.chosen_names:
                    other, entry = self.chosen_names[candidate]
                    return "chosen", (other, entry["section"] - 1, entry["value"])
            address = self.program.bound(coff.source, name)
            if address is not None:
                return "address", address
            found = self.program.globals().get(name) or self.program.globals().get(c_symbol(name))
            if found is None:
                raise Unresolved(f"{name} (referenced by {coff.source})")
            other, entry = found
            return self.target(other, entry)
        section = coff.sections[symbol["section"] - 1]
        named = not name.startswith(".") and not LITERAL.fullmatch(name)
        if named and section["code"]:
            for candidate in dict.fromkeys((name, c_symbol(name))):
                if (coff.source, candidate) in self.chosen:
                    return "chosen", (coff, symbol["section"] - 1, symbol["value"])
            address = self.program.bound(coff.source, name)
            if address is not None:
                return "address", address
        elif named:
            address = self.program.bound(coff.source, name)
            if address is not None:
                return "address", address
        if named:
            self.copied.setdefault(name, coff.source)
        return "copy", (coff, symbol["section"] - 1, symbol["value"])

    def closure(self, coff, index, seen):
        """Check that section INDEX of COFF and everything it copies with it resolve (not the chosen functions it
        references: each is checked on its own, and one that cannot link is not chosen). Raises Unresolved."""
        if (coff.source, index) in seen:
            return
        seen.add((coff.source, index))
        for _, symbol_index, kind in coff.sections[index]["relocs"]:
            if kind not in (6, 7, 20):
                raise Unresolved(f"relocation type {kind} in {coff.source}")
            kind, value = self.target(coff, coff.symbols[symbol_index])
            if kind == "copy":
                self.closure(value[0], value[1], seen)

    def copy(self, coff, index):
        key = (coff.source, index)
        if key not in self.placed:
            section = coff.sections[index]
            self.placed[key] = self.pe.place(section["data"], 16 if section["code"] else 8)
            self.pending.append((coff, index))
        return self.placed[key]

    def address(self, coff, symbol):
        kind, value = self.target(coff, symbol)
        if kind == "address":
            return value
        other, index, offset = value
        return self.copy(other, index) + offset

    def relocate(self):
        while self.pending:
            coff, index = self.pending.pop(0)
            base = self.placed[(coff.source, index)]
            body = bytearray(coff.sections[index]["data"])
            for offset, symbol_index, kind in coff.sections[index]["relocs"]:
                target = self.address(coff, coff.symbols[symbol_index])
                addend = struct.unpack_from("<i", body, offset)[0]
                if kind == 6:
                    value = target + addend
                    self.pe.added_fixups.add(base + offset)
                elif kind == 7:
                    value = target - self.pe.image_base + addend
                else:
                    value = target + addend - (base + offset + 4)
                struct.pack_into("<I", body, offset, value & 0xFFFFFFFF)
            start = base - self.pe.new_base
            self.pe.new[start:start + len(body)] = body


def select(program, args, exact=None):
    """The keys (SOURCE:NAME) the arguments choose, in the original's order."""
    every = sourced(program)
    keys = set()
    if args.exact:
        keys |= exact if exact is not None else exact_functions(program.root)
    if args.all_sourced:
        keys |= {key for key, _ in every}
    for source in args.source:
        matched = {key for key, f in every if f["source"] == source}
        if not matched:
            raise SystemExit(f"{source}: no function of config/{VERSION}/functions.json has this source")
        keys |= matched
    for name in args.function:
        matched = {key for key, f in every if name in (key, f["name"])}
        if not matched:
            raise SystemExit(f"{name}: no function with a source has this name")
        keys |= matched
    excluded = list(args.exclude)
    for path in args.exclude_file:
        excluded += [line.split("#", 1)[0].strip() for line in Path(path).read_text().splitlines()]
    excluded = {e for e in excluded if e}
    keys = {key for key in keys if not {key, *key.rsplit(":", 1)} & excluded}
    return [key for key, _ in every if key in keys]


def build(keys, program=None, original=ORIGINAL):
    """The hybrid executable's bytes for KEYS, and its manifest."""
    program = program or Program()
    pe = PE(original)
    linker = Linker(program, pe)
    entries = {key: f for key, f in sourced(program)}
    skipped = {}
    candidates = []
    for key in keys:
        f = entries[key]
        coff = program.obj(f["source"])
        symbol = coff.defined(function_symbol(f["name"])) if coff else None
        if symbol is None:
            skipped[key] = "not emitted by its source's object" if coff else "its source's object was not built"
            continue
        candidates.append((key, f, coff, symbol))
    linker.chosen = {(c[2].source, function_symbol(c[1]["name"])): (c[2], c[3]) for c in candidates}
    # (a function whose copy cannot link is dropped; a reference to it then goes to the original)
    while True:
        dropped = []
        for key, f, coff, symbol in candidates:
            try:
                linker.closure(coff, symbol["section"] - 1, set())
            except Unresolved as error:
                skipped[key] = f"unresolved: {error}"
                dropped.append(key)
        if not dropped:
            break
        candidates = [c for c in candidates if c[0] not in dropped]
        linker.chosen = {(c[2].source, function_symbol(c[1]["name"])): (c[2], c[3]) for c in candidates}
    linker.copied = {}
    injected = []
    for key, f, coff, symbol in candidates:
        address = int(f["address"], 0)
        start, end = symbol["value"], coff.function_end(symbol)
        section = coff.sections[symbol["section"] - 1]
        if f["size"] < 5:
            local = [r for r in section["relocs"] if start <= r[0] < end]
            if end - start <= f["size"] and not local:
                pe.write(address, section["data"][start:end] + b"\xCC" * (f["size"] - (end - start)))
                injected.append(dict(key=key, address=f"0x{address:08x}", copy="in place"))
            else:
                skipped[key] = "too short for a jump"
                linker.chosen = {k: v for k, v in linker.chosen.items() if k != (coff.source, function_symbol(f["name"]))}
            continue
        target = linker.copy(coff, symbol["section"] - 1) + start
        pe.write(address, b"\xE9" + struct.pack("<i", target - (address + 5)))
        injected.append(dict(key=key, address=f"0x{address:08x}", copy=f"0x{target:08x}"))
    linker.relocate()
    image = pe.image()
    manifest = dict(
        original=str(original.relative_to(program.root) if original.is_relative_to(program.root) else original),
        sha1=hashlib.sha1(image).hexdigest(), new_section=f"0x{pe.new_base:08x}", new_bytes=len(pe.new),
        injected=injected, skipped=dict(sorted(skipped.items())),
        copied=dict(sorted(linker.copied.items())))
    return image, manifest


def write(output, image, manifest):
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(image)
    output.chmod(0o755)
    Path(str(output) + ".json").write_text(json.dumps(manifest, indent=1) + "\n")
    # (the compiler loads its license manager DLL from its own directory)
    for sibling in ORIGINAL.parent.iterdir():
        if sibling.suffix.lower() == ".dll" and not (output.parent / sibling.name).exists():
            (output.parent / sibling.name).symlink_to(sibling.resolve())


def parser():
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    p.add_argument("--exact", action="store_true")
    p.add_argument("--all-sourced", action="store_true")
    p.add_argument("--source", action="append", default=[])
    p.add_argument("--function", action="append", default=[])
    p.add_argument("--exclude", action="append", default=[])
    p.add_argument("--exclude-file", action="append", default=[])
    return p


def main():
    p = parser()
    p.add_argument("output")
    p.add_argument("--list", action="store_true", help="print the selection and stop")
    args = p.parse_args()
    program = Program()
    keys = select(program, args)
    if args.list:
        print("\n".join(keys))
        return
    if not keys:
        raise SystemExit("nothing selected")
    image, manifest = build(keys, program)
    write(args.output, image, manifest)
    for name, source in manifest["copied"].items():
        print(f"copied: {name} ({source})", file=sys.stderr)
    for key, reason in manifest["skipped"].items():
        print(f"skipped: {key}: {reason}", file=sys.stderr)
    print(f"{args.output}: {len(manifest['injected'])} functions injected, {len(manifest['skipped'])} skipped, "
          f"{manifest['new_bytes']} bytes, sha1 {manifest['sha1']}")


if __name__ == "__main__":
    main()
