"""Relocation-resolution tests using small original-image fixtures."""
import struct
import unittest

from compare import resolve_function


class OriginalImage:
    image_base = 0x400000

    def __init__(self, blocks):
        self.blocks = blocks

    def read(self, address, size):
        for start, data in self.blocks.items():
            offset = address - start
            if 0 <= offset and offset + size <= len(data):
                return data[offset:offset + size]
        raise ValueError("unmapped original address")

    def contains(self, address, needle):
        try:
            return self.read(address, len(needle)) == needle
        except ValueError:
            return False

    def find(self, needle, limit=None):
        found = []
        for start, data in self.blocks.items():
            offset = -1
            while (offset := data.find(needle, offset + 1)) >= 0:
                found.append(start + offset)
                if limit and len(found) == limit:
                    return found
        return found


def candidate(literal, name="_@1", section=2):
    symbols = {
        0: dict(name="_function", value=0, section=1, type=0x20, storage=2),
        1: dict(name=name, value=0, section=section, type=0, storage=3),
    }
    sections = [dict(code=True, data=bytes(4), relocs=[(0, 1, 6)]),
                dict(code=False, data=literal, relocs=[])]
    return symbols, sections


class LiteralResolutionTests(unittest.TestCase):
    def test_generated_labels_collide_across_translation_units(self):
        original = OriginalImage({0x500000: b"alpha\0", 0x500010: b"beta\0"})
        for literal, address in ((b"alpha\0", 0x500000), (b"beta\0", 0x500010)):
            body, _ = resolve_function(*candidate(literal), "_function", 0x401000,
                                       {"_@1": 0x500000}, original, content_literals=True)
            self.assertEqual(body, struct.pack("<I", address))

    def test_numeric_local_label_cannot_hide_unrecoverable_data(self):
        original = OriginalImage({0x500000: b"old table\0"})
        with self.assertRaisesRegex(ValueError, "unbound literal"):
            resolve_function(*candidate(b"new table\0"), "_function", 0x401000,
                             {"_@1": 0x500000}, original, content_literals=True)

    def test_ambiguous_literal_uses_original_function_reference(self):
        original = OriginalImage({0x401000: struct.pack("<I", 0x500010),
                                  0x500000: b"same\0", 0x500010: b"same\0"})
        body, _ = resolve_function(*candidate(b"same\0", name="@1"), "_function", 0x401000,
                                   {"@1": 0x500000}, original, 4, content_literals=True)
        self.assertEqual(body, struct.pack("<I", 0x500010))

    def test_external_binding_remains_authoritative(self):
        body, _ = resolve_function(*candidate(b"", section=0), "_function", 0x401000,
                                   {"_@1": 0x500010}, OriginalImage({}), content_literals=True)
        self.assertEqual(body, struct.pack("<I", 0x500010))

    def test_named_storage_keeps_its_explicit_binding(self):
        body, _ = resolve_function(*candidate(b"data", name="_storage"), "_function", 0x401000,
                                   {"_storage": 0x500010}, OriginalImage({}), content_literals=True)
        self.assertEqual(body, struct.pack("<I", 0x500010))

    def test_legacy_local_binding_requires_explicit_migration(self):
        original = OriginalImage({0x500000: b"old\0", 0x500010: b"new\0"})
        body, _ = resolve_function(*candidate(b"new\0"), "_function", 0x401000,
                                   {"_@1": 0x500000}, original)
        self.assertEqual(body, struct.pack("<I", 0x500000))


if __name__ == "__main__":
    unittest.main()
