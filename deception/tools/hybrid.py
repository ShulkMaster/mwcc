#!/usr/bin/env python3
"""python deception/tools/hybrid.py OUTPUT [--inject OBJ:FUNCTION ...] [--hook OBJ]

A hybrid compiler: the original GC 2.7 executable with functions of compiled sources linked into a new section.
- --inject OBJ:FUNCTION: FUNCTION of OBJ replaces the original's (a jump at the original's entry).
- --hook OBJ: the AST oracle; its AstDump_CodeGen_Generator runs in front of the original CodeGen_Generator (a detour,
  the original reached through CodeGen_Generator_Original).
Symbols resolve to the original's addresses: config/GC_2_7/functions.json, config/GC_2_7/bindings.json, then
deception/config/bindings.json. A compiled source's literals (its sections of data) are copied with it.
"""
import argparse
import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ORIGINAL = ROOT / "build/compilers/GC/2.7/mwcceppc.exe"
HOOKS = {"CodeGen_Generator": "AstDump_CodeGen_Generator"}


class Coff:
    def __init__(self, path):
        self.path = path
        data = Path(path).read_bytes()
        machine, count, _, symbols, nsym, optional, _ = struct.unpack_from("<HHIIIHH", data)
        if machine != 0x14C or optional:
            raise SystemExit(f"{path}: not a relocatable i386 COFF")
        strings = data[symbols + nsym * 18:]

        def name(raw):
            if raw[:4] == b"\0\0\0\0":
                offset = struct.unpack_from("<I", raw, 4)[0]
                return strings[offset:strings.index(b"\0", offset)].decode()
            return raw.rstrip(b"\0").decode()

        self.sections = []
        for i in range(count):
            offset = 20 + i * 40
            raw_name = data[offset:offset + 8]
            size, raw, relocs, _, nrel, _, flags = struct.unpack_from("<IIIIHHI", data, offset + 16)
            self.sections.append(dict(
                name=name(raw_name) if not raw_name.startswith(b"/") else raw_name.decode(),
                flags=flags, code=bool(flags & 0x20),
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

    def function(self, name):
        """(section index, offset) of the function NAME (its C or C++ symbol)."""
        for symbol in self.symbols.values():
            if symbol["section"] > 0 and symbol["type"] & 0x20 and symbol["name"] in (name, "_" + name):
                return symbol["section"] - 1, symbol["value"]
        raise SystemExit(f"{self.path}: no function {name}")


class PE:
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
        """A new base relocation table, in the new section: the loader relocates the image (wibo cannot map it at
        its preferred base)."""
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

    def save(self, path):
        self.write_base_relocations()
        raw = (len(self.data) + self.file_alignment - 1) & -self.file_alignment
        size = (len(self.new) + self.file_alignment - 1) & -self.file_alignment
        header = struct.pack("<8sIIIIIIHHI", b".hybrid", len(self.new), self.new_rva, size, raw, 0, 0, 0, 0,
                             0xE0000020)
        self.data[self.table + self.count * 40:self.table + self.count * 40 + 40] = header
        struct.pack_into("<H", self.data, self.header + 6, self.count + 1)
        image = (self.new_rva + len(self.new) + self.section_alignment - 1) & -self.section_alignment
        struct.pack_into("<I", self.data, self.optional + 56, image)
        out = self.data + bytes(raw - len(self.data)) + self.new + bytes(size - len(self.new))
        Path(path).write_bytes(out)
        Path(path).chmod(0o755)


class Linker:
    def __init__(self, pe):
        self.pe = pe
        functions = json.loads((ROOT / "config/GC_2_7/functions.json").read_text())
        self.addresses = {f["name"]: int(f["address"], 0) for f in functions}
        for path in (ROOT / "config/GC_2_7/bindings.json", ROOT / "deception/config/bindings.json"):
            if path.exists():
                for name, value in json.loads(path.read_text()).items():
                    self.addresses[name] = int(value, 0)
        self.placed = {}  # (obj path, section index) -> address of its copy
        self.objects = {}
        self.injected = {}  # symbol name -> new address
        self.pending = []
        self.missing = set()
        self.implicit = set()  # (functions of an injected one's source the original has no address for: copied)
        self.hook = None

    def obj(self, path):
        if path not in self.objects:
            self.objects[path] = Coff(path)
        return self.objects[path]

    def lookup(self, name):
        for candidate in (name, name[1:] if name.startswith("_") else None):
            if candidate and candidate in self.injected:
                return self.injected[candidate]
        for candidate in (name, name[1:] if name.startswith("_") else None):
            if candidate and candidate in self.addresses:
                return self.addresses[candidate]
        return None

    def copy_section(self, coff, index):
        """The address of COFF's section INDEX in the new section, copying it (and its relocations) once."""
        key = (coff.path, index)
        if key not in self.placed:
            section = coff.sections[index]
            self.placed[key] = self.pe.place(section["data"], 16 if section["code"] else 8)
            self.pending.append((coff, index))
        return self.placed[key]

    def symbol_address(self, coff, symbol):
        if symbol["section"] == -1:
            return symbol["value"]
        if symbol["section"] == 0:
            return self.lookup(symbol["name"])
        section = coff.sections[symbol["section"] - 1]
        if section["code"]:
            # (another function of the same source: the copy when it is injected, else the original's)
            if symbol["type"] & 0x20:
                address = self.lookup(symbol["name"])
                if address is not None:
                    return address
            if coff.path != self.hook:
                self.implicit.add(symbol["name"])
            return self.copy_section(coff, symbol["section"] - 1) + symbol["value"]
        return self.copy_section(coff, symbol["section"] - 1) + symbol["value"]

    def relocate(self):
        while self.pending:
            coff, index = self.pending.pop()
            base = self.placed[(coff.path, index)]
            section = coff.sections[index]
            body = bytearray(section["data"])
            for offset, symbol_index, kind in section["relocs"]:
                symbol = coff.symbols[symbol_index]
                target = self.symbol_address(coff, symbol)
                if target is None:
                    self.missing.add((symbol["name"], str(coff.path)))
                    continue
                addend = struct.unpack_from("<i", body, offset)[0]
                if kind == 6:
                    value = target + addend
                    self.pe.added_fixups.add(base + offset)
                elif kind == 7:
                    value = target - self.pe.image_base + addend
                elif kind == 20:
                    value = target + addend - (base + offset + 4)
                else:
                    raise SystemExit(f"{coff.path}: relocation type {kind}")
                struct.pack_into("<I", body, offset, value & 0xFFFFFFFF)
            start = base - self.pe.new_base
            self.pe.new[start:start + len(body)] = body

    def inject(self, path, name):
        coff = self.obj(path)
        index, offset = coff.function(name)
        self.injected[name] = self.copy_section(coff, index) + offset

    def entry_jump(self, original, target):
        self.pe.write(original, b"\xE9" + struct.pack("<i", target - (original + 5)))


def instruction_length(code):
    """The length of a prologue instruction (the few kinds a detour copies), else None."""
    if 0x50 <= code[0] <= 0x57:
        return 1
    if code[0] in (0x89, 0x8B) and code[1] >> 6 == 3:
        return 2
    if code[:2] == b"\x83\xEC":
        return 3
    if code[:2] == b"\x81\xEC":
        return 6
    return None


def detour_bytes(pe, address):
    """Whole instructions at ADDRESS covering five bytes."""
    size = 0
    while size < 5:
        length = instruction_length(pe.read(address + size, 8))
        if length is None:
            raise SystemExit(f"0x{address + size:08x}: cannot detour this instruction")
        size += length
    return pe.read(address, size)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("output")
    parser.add_argument("--inject", action="append", default=[], help="OBJ:FUNCTION")
    parser.add_argument("--hook", help="the AST oracle's object")
    args = parser.parse_args()
    pe = PE(ORIGINAL)
    linker = Linker(pe)
    replaced = []
    for item in args.inject:
        path, name = item.rsplit(":", 1)
        linker.inject(Path(path), name)
        replaced.append(name)
    if args.hook:
        linker.hook = Path(args.hook)
        for original, hook in HOOKS.items():
            address = linker.addresses[original]
            stolen = detour_bytes(pe, address)
            trampoline = pe.place(stolen + b"\xE9" + struct.pack("<i", 0))
            struct.pack_into("<i", pe.new, trampoline - pe.new_base + len(stolen) + 1,
                             address + len(stolen) - (trampoline + len(stolen) + 5))
            linker.injected[original + "_Original"] = trampoline
            linker.inject(Path(args.hook), hook)
        linker.inject(Path(args.hook), "AstDump_Hit")
    linker.relocate()
    if linker.missing:
        for name, path in sorted(linker.missing):
            print(f"unresolved: {name} ({path})", file=sys.stderr)
        raise SystemExit(1)
    for index, name in enumerate(replaced):
        if name not in linker.addresses:
            raise SystemExit(f"{name}: the original's address is not known (config/GC_2_7/functions.json)")
        target = linker.injected[name]
        if args.hook:
            # (an entry stub counting the calls: registers and flags kept, then the injected function)
            stub = pe.new_base + ((len(pe.new) + 15) & -16)
            hit = linker.injected["AstDump_Hit"]
            code = (b"\x60\x9C\x68" + struct.pack("<i", index) + b"\xE8" + struct.pack("<i", hit - (stub + 12))
                    + b"\x83\xC4\x04\x9D\x61\xE9" + struct.pack("<i", target - (stub + 22)))
            if pe.place(code) != stub:
                raise SystemExit("stub placement")
            target = stub
        linker.entry_jump(linker.addresses[name], target)
    if args.hook:
        Path(str(args.output) + ".hits.json").write_text(json.dumps(replaced, indent=1) + "\n")
    if args.hook:
        for original, hook in HOOKS.items():
            linker.entry_jump(linker.addresses[original], linker.injected[hook])
    for name in sorted(linker.implicit):
        print(f"copied with its caller (no address in the original): {name}", file=sys.stderr)
    pe.save(args.output)
    print(f"{args.output}: {len(replaced)} functions injected, {len(pe.new)} bytes{', AST oracle' if args.hook else ''}")


if __name__ == "__main__":
    main()
