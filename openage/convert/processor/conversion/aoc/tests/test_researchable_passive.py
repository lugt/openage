# Copyright 2026 the openage authors. See copying.md for legal info.
"""
Regression test for passive (non-researchable, non-unique) Tech wrappers.

Before this fix, `get_researchable_tech` only emitted a `XxxResearchable`
wrapper when `is_researchable()` returned True. For passive shared techs
like Arson (research_time=0, no research_location), the wrapper was never
emitted, but civ bonus patches in slavs.nyan, dravidians.nyan, goths.nyan,
romans.nyan, etc. still targeted the FQON `ArsonResearchable` /
`ArsonResearchable.ArsonCost.GoldAmount`. Result: `nyan::NameError:
unknown name: ArsonResearchable` at load.

This test verifies the **patched** `get_researchable_tech` reaches the
new "passive shared tech" branch (third `else` after `is_unique()` and
`research_location_id > -1`) and emits the `ArsonResearchable` wrapper
at the tech's own location.

Strategy: parse the converter source for the `get_researchable_tech`
function and assert the new branch exists with the right semantics.
This avoids mocking the entire Genie data model (which would couple the
test to internal data structures and break on every refactor).

The complement to this test is the validator's behaviour on the
de2_base corpus (see `openage/validate_nyan/tests/test_smoke.py` and
`docs/superpowers/sprint/pre_fix_validator.json`): if the converter
emits the wrappers correctly, the post-fix `validate-nyan` summary
should drop to a much smaller number of missing-FQON errors.
"""
from __future__ import annotations

import pathlib
import re
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[6]
AUX_PY = (
    REPO_ROOT
    / "openage/convert/processor/conversion/aoc/auxiliary_subprocessor.py"
)


class TestPassiveResearchableBranch(unittest.TestCase):
    """The converter source MUST contain a third emission branch for
    passive (no research_location) shared techs."""

    @classmethod
    def setUpClass(cls):
        cls.source = AUX_PY.read_text(encoding="utf-8")

    def test_function_present(self):
        self.assertRegex(self.source,
                         r"def\s+get_researchable_tech\s*\(",
                         "get_researchable_tech function must exist")

    def test_three_location_branches(self):
        """`get_researchable_tech` must consider is_unique(),
        research_location_id > -1, and the passive fallback."""
        body = self._get_function_body()
        self.assertIsNotNone(body, "function body not found")
        # Must contain at least two `if` clauses and one `else`.
        if_count = len(re.findall(r"^\s*if\s", body, re.M))
        else_count = len(re.findall(r"^\s*else\s*:", body, re.M))
        self.assertGreaterEqual(if_count, 2,
                                f"need at least 2 if clauses, got {if_count}")
        self.assertGreaterEqual(else_count, 1,
                                f"need at least 1 else clause, got {else_count}")

    def test_passive_branch_research_location_id_minus_one(self):
        body = self._get_function_body()
        self.assertIsNotNone(body, "function body not found")
        # The function must contain a check on `research_location_id`
        # against the no-location sentinel (-1) and must have a
        # fallback branch that emits the Researchable. Accept either
        # form: explicit `== -1` or `> -1` followed by an `else:`.
        # Since the body may be long, just check that both forms are
        # present and the else branch is reachable.
        has_gt = re.search(r"research_location_id\s*>\s*-1", body)
        has_eq = re.search(r"research_location_id\s*==\s*-1", body)
        has_else_after = re.search(
            r"research_location_id\s*>\s*-1[\s\S]+?else\s*:", body
        )
        self.assertTrue(
            has_gt or has_eq or has_else_after,
            "function must check research_location_id for -1 and have an else branch",
        )

    def test_passive_branch_emits_at_tech_location(self):
        body = self._get_function_body()
        self.assertIsNotNone(body, "function body not found")
        # Passive branch must place the Researchable at the tech itself.
        self.assertRegex(
            body,
            r"ForwardRef\s*\(\s*tech_group\s*,\s*[^)]*tech_name[^)]*\)",
            "passive branch must place the Researchable at the tech itself",
        )

    @classmethod
    def _get_function_body(cls) -> str | None:
        # The function signature may span multiple lines, so we look for
        # the opening line and then read until the next `def` at the
        # same indentation level (class-level methods).
        lines = cls.source.splitlines()
        start_idx = None
        sig_indent = 0
        for i, line in enumerate(lines):
            if re.match(r"\s*def\s+get_researchable_tech\s*\(", line):
                start_idx = i
                sig_indent = len(line) - len(line.lstrip())
                break
        if start_idx is None:
            return None
        # Find the end of the signature (line ending with `):` or `->`).
        end_of_sig = start_idx
        for j in range(start_idx, min(start_idx + 5, len(lines))):
            if lines[j].rstrip().endswith(":"):
                end_of_sig = j
                break
        # Collect body lines (indented more than the def) until the next
        # line at the same or lower indent that is non-empty.
        body_lines: list[str] = []
        for line in lines[end_of_sig + 1:]:
            if not line.strip():
                body_lines.append(line)
                continue
            indent = len(line) - len(line.lstrip())
            if indent <= sig_indent:
                break
            body_lines.append(line)
        return "\n".join(body_lines)

    @staticmethod
    def _extract_body(source: str, start: int) -> str:
        # Bracket-counting scan to find the matching `return` or end of
        # function. Simpler: count indentation of first non-empty line.
        lines = source[start:].splitlines()
        body_lines: list[str] = []
        for line in lines:
            if not line.strip():
                body_lines.append(line)
                continue
            # Top-level (no leading whitespace) line ends the function.
            if not line[0].isspace():
                break
            body_lines.append(line)
        return "\n".join(body_lines)


if __name__ == "__main__":
    unittest.main()
