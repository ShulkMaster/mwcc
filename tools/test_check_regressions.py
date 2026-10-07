"""Focused semantic tests for the landing gate; no compiler/build required."""
import copy
import unittest

from check_regressions import SCHEMA, diff_state, inventory_identities, regressions


def instruction(address, kind=None, args=None):
    row = {"instruction": {"address": str(address), "size": 1, "parts": [
        {"opcode": {"mnemonic": "mov"}}, {"arg": {"opaque": "eax"}}, {"arg": {"opaque": "ebx"}}]}}
    if kind:
        row["diff_kind"] = kind
    if args is not None:
        row["arg_diff"] = args
    return row


def snapshot(rows, right=None):
    symbols = [{"name": "f", "kind": "SYMBOL_FUNCTION", "size": "2", "target_symbol": 0,
                "instructions": rows}]
    state = diff_state({"left": {"symbols": symbols}, "right": {"symbols": [
        {"name": "f", "instructions": right if right is not None else rows}]}})
    state.update(target="original", base="base", complete=True, mapped=True)
    return {"schema": SCHEMA, "version": "GC_2_7", "units": {"unit": state}}


def branch(address, destination):
    return {"instruction": {"address": str(address), "size": 5, "parts": [
        {"opcode": {"mnemonic": "call"}}, {"arg": {"branch_dest": str(destination)}}]}}


class RowGateTests(unittest.TestCase):
    def test_normalized_external_call_is_not_a_match(self):
        captured = snapshot([branch(0, 28272)], [branch(0, 27808)])
        function = next(iter(captured["units"]["unit"]["functions"].values()))
        self.assertFalse(function["exact"])
        self.assertFalse(function["rows"]["0"]["full"])
        self.assertTrue(function["rows"]["0"]["opcode"])
        self.assertEqual(function["rows"]["0"]["args"], [False])

    def test_external_call_destination_regression_is_rejected(self):
        before = snapshot([branch(0, 28272)])
        after = snapshot([branch(0, 28272)], [branch(0, 27808)])
        self.assertTrue(any("matched argument 0 lost" in e for e in regressions(before, after)))

    def test_correcting_external_call_is_an_improvement(self):
        before = snapshot([branch(0, 28272)], [branch(0, 27808)])
        after = snapshot([branch(0, 28272)])
        after["units"]["unit"]["base"] = "corrected"
        self.assertEqual(regressions(before, after), [])

    def test_internal_branch_destination_uses_aligned_target_row(self):
        before = snapshot([branch(0, 5), instruction(5)], [branch(0, 8), instruction(8)])
        function = next(iter(before["units"]["unit"]["functions"].values()))
        self.assertTrue(function["rows"]["0"]["full"])
        self.assertEqual(function["rows"]["0"]["args"], [True])

    def test_internal_branch_redirected_to_another_row_is_rejected(self):
        rows = [branch(0, 5), instruction(5), instruction(6)]
        before = snapshot(rows, [branch(0, 8), instruction(8), instruction(9)])
        after = snapshot(rows, [branch(0, 9), instruction(8), instruction(9)])
        self.assertTrue(any("matched argument 0 lost" in e for e in regressions(before, after)))

    def test_same_percentage_gain_hides_loss(self):
        before = snapshot([instruction(0), instruction(1, "DIFF_REPLACE")])
        after = snapshot([instruction(0, "DIFF_REPLACE"), instruction(1)])
        self.assertTrue(any("matched instruction lost" in e for e in regressions(before, after)))

    def test_argument_gain_hides_other_argument_loss(self):
        before = snapshot([instruction(0, "DIFF_ARG_MISMATCH", [{}, {"diff_index": 0}])])
        after = snapshot([instruction(0, "DIFF_ARG_MISMATCH", [{"diff_index": 0}, {}])])
        self.assertTrue(any("argument 0 lost" in e for e in regressions(before, after)))

    def test_opcode_mismatch_keeps_matching_operands_protected(self):
        before = snapshot([instruction(0, "DIFF_OP_MISMATCH", [{}, {}])])
        after = snapshot([instruction(0, "DIFF_ARG_MISMATCH", [{"diff_index": 0}, {}])])
        self.assertTrue(any("argument 0 lost" in e for e in regressions(before, after)))
        self.assertEqual(regressions(before, copy.deepcopy(before)), [])

    def test_neutral_and_improvement(self):
        before = snapshot([instruction(0), instruction(1, "DIFF_REPLACE")])
        self.assertEqual(regressions(before, copy.deepcopy(before)), [])
        self.assertEqual(regressions(before, snapshot([instruction(0), instruction(1)])), [])

    def test_missing_function_unit_and_mapping(self):
        before = snapshot([instruction(0)])
        after = copy.deepcopy(before)
        after["units"]["unit"]["functions"] = {}
        self.assertTrue(regressions(before, after))
        after["units"] = {}
        self.assertTrue(regressions(before, after))
        after = copy.deepcopy(before)
        after["units"]["unit"]["mapped"] = False
        self.assertTrue(regressions(before, after))

    def test_insertion_at_anchor_is_rejected(self):
        before = snapshot([instruction(0), instruction(1, "DIFF_REPLACE")])
        after = snapshot([instruction(0), {"diff_kind": "DIFF_INSERT"}, instruction(1, "DIFF_REPLACE")],
                         [instruction(0), instruction(2), instruction(1)])
        self.assertTrue(any("insertion" in e for e in regressions(before, after)))

    def test_existing_extra_row_can_change_an_unmatched_operand(self):
        rows = [instruction(0), {"diff_kind": "DIFF_INSERT"}, instruction(1, "DIFF_REPLACE")]
        right = [instruction(0), instruction(2), instruction(1, "DIFF_REPLACE")]
        before = snapshot(rows, right)
        right = copy.deepcopy(right)
        right[1]["instruction"]["parts"][2] = {"arg": {"opaque": "ecx"}}
        after = snapshot(rows, right)
        self.assertFalse(regressions(before, after))

    def test_existing_extra_row_does_not_allow_insertion_growth(self):
        before = snapshot([instruction(0), {"diff_kind": "DIFF_INSERT"}, instruction(1, "DIFF_REPLACE")],
                          [instruction(0), instruction(2), instruction(1, "DIFF_REPLACE")])
        after = snapshot([instruction(0), {"diff_kind": "DIFF_INSERT"}, {"diff_kind": "DIFF_INSERT"},
                          instruction(1, "DIFF_REPLACE")],
                         [instruction(0), instruction(2), instruction(3), instruction(1, "DIFF_REPLACE")])
        self.assertTrue(any("insertion" in e for e in regressions(before, after)))

    def test_exact_bytes_and_data_preservation(self):
        before = snapshot([instruction(0)])
        after = copy.deepcopy(before)
        after["units"]["unit"]["base"] = "changed"
        self.assertTrue(any("exact base" in e for e in regressions(before, after)))
        before = snapshot([instruction(0, "DIFF_REPLACE")])
        before["units"]["unit"]["data"] = "data"
        after = copy.deepcopy(before)
        after["units"]["unit"]["base"] = "changed"
        self.assertTrue(any("non-code" in e for e in regressions(before, after)))

    def test_bad_coverage_fails_closed(self):
        with self.assertRaises(ValueError):
            snapshot([instruction(0, "DIFF_ARG_MISMATCH", [{}])])
        with self.assertRaises(ValueError):
            regressions({}, {})

    def test_symbol_recovery_does_not_change_identity(self):
        rows = [instruction(0)]
        left = {"name": "fn_004c4410", "kind": "SYMBOL_FUNCTION", "size": "1",
                "target_symbol": 0, "instructions": rows}
        original = diff_state({"left": {"symbols": [left]}, "right": {"symbols": [left]}})
        renamed = dict(left, name="CInt64_Sub")
        recovered = diff_state({"left": {"symbols": [renamed]}, "right": {"symbols": [renamed]}})
        self.assertEqual(original, recovered)

    def test_original_alias_views_are_checked_independently(self):
        inventory = [
            {"name": "conversion", "address": "0x004fe050", "size": 104, "source": "src/frontend/CMangler.c"},
            {"name": "rtti", "address": "0x004fe050", "size": 104, "source": "src/frontend/CMangler.c"},
            {"name": "unique", "address": "0x004fe100", "size": 8, "source": "src/frontend/CMangler.c"},
        ]
        identities = inventory_identities(inventory)
        self.assertEqual(identities["src/frontend/CMangler/unique"], "004fe100:8")
        first = identities["src/frontend/CMangler/conversion"]
        second = identities["src/frontend/CMangler/rtti"]
        self.assertNotEqual(first, second)
        before = snapshot([instruction(0), instruction(1, "DIFF_REPLACE")])
        unit = before["units"].pop("unit")
        before["units"] = {first: copy.deepcopy(unit), second: copy.deepcopy(unit)}
        after = copy.deepcopy(before)
        after["units"][first] = snapshot([instruction(0), instruction(1)])["units"]["unit"]
        after["units"][second] = snapshot([instruction(0, "DIFF_REPLACE"), instruction(1)])["units"]["unit"]
        errors = regressions(before, after)
        self.assertTrue(any(second in e and "matched instruction lost" in e for e in errors))
        self.assertFalse(any(first in e for e in errors))


if __name__ == "__main__":
    unittest.main()
