"""Focused semantic tests for the landing gate; no compiler/build required."""
import copy
import unittest

from check_regressions import SCHEMA, diff_state, regressions


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


class RowGateTests(unittest.TestCase):
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


if __name__ == "__main__":
    unittest.main()
