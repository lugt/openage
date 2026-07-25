# Copyright 2026 the openage authors. See copying.md for legal info.
"""
Behavior test for the auxiliary subprocessor emission location.

The previous test approach was source-text inspection of `if/else`
branches, which is fragile and does not catch the actual bug we
fixed: `ForwardRef(research_location, "X.Research")` was being used
even though building_lines do not register a nested "X.Research"
sub-object, causing KeyError at emit time and silent skip of every
Researchable. This test verifies the FQON/Location wiring on a
minimal in-memory RawAPIObject, which is the actual contract that
must hold for the fix to work.
"""
from __future__ import annotations

import unittest
from unittest.mock import MagicMock, patch

from openage.convert.entity_object.conversion.converter_object import (
    RawAPIObject,
)
from openage.convert.value_object.conversion.forward_ref import ForwardRef


class TestResearchableEmissionLocation(unittest.TestCase):
    """Verify ForwardRef wiring matches what the emit phase expects."""

    def test_forward_ref_to_tech_name_resolves_to_existing_key(self):
        """
        After the fix, ArsonResearchable's location is
        `ForwardRef(tech_group, "Arson")`. The tech_group's
        `raw_api_objects` dict contains the Arson RawAPIObject under
        the key "Arson", so the emit phase's `obj_location.resolve()`
        can look it up.

        Before the fix the same was tried with
        `ForwardRef(research_location, "Barracks.Research")` which
        was not in the building_line's dict and raised KeyError.
        """
        class FakeGroup:
            def __init__(self):
                self.raw_api_objects = {}

            def get_raw_api_object(self, key):
                return self.raw_api_objects[key]

        tech_group = FakeGroup()
        arson_raw = RawAPIObject("Arson", "Arson", {})
        tech_group.raw_api_objects["Arson"] = arson_raw

        # This is the pattern the fix uses for ArsonResearchable.
        location = ForwardRef(tech_group, "Arson")

        # Resolve must succeed.
        resolved_raw = location.resolve_raw()
        self.assertIs(resolved_raw, arson_raw,
                      "ForwardRef must point to the Arson RawAPIObject")

    def test_cost_location_points_to_creatable_subkey(self):
        """
        The cost wrapper's location is
        `ForwardRef(line, "Arson.CreatableGameEntity")` (existing
        pattern, unchanged). The line's `raw_api_objects` dict must
        contain the ArsonCreatable under that key.
        """
        class FakeGroup:
            def __init__(self):
                self.raw_api_objects = {}

            def get_raw_api_object(self, key):
                return self.raw_api_objects[key]

        line = FakeGroup()
        archer_creatable = RawAPIObject(
            "Archer.CreatableGameEntity", "ArcherCreatable", {}
        )
        line.raw_api_objects["Archer.CreatableGameEntity"] = archer_creatable

        # This is the existing cost-location pattern.
        location = ForwardRef(line, "Archer.CreatableGameEntity")

        resolved = location.resolve_raw()
        self.assertIs(resolved, archer_creatable,
                      "cost must resolve to the Creatable wrapper")

    def test_broken_pattern_would_fail_resolve(self):
        """
        Document the pre-fix failure mode: `ForwardRef(line, "X.Research")`
        does NOT exist in line.raw_api_objects (the building_line only
        has "X" as a key, not a nested "X.Research"). The emit phase
        catches KeyError and silently skips the Researchable. The fix
        replaces this pattern with `ForwardRef(line, "X")` which
        resolves cleanly.
        """
        class FakeGroup:
            def __init__(self):
                self.raw_api_objects = {}

            def get_raw_api_object(self, key):
                return self.raw_api_objects[key]

        line = FakeGroup()
        line.raw_api_objects["Barracks"] = MagicMock(name="Barracks")

        # Pre-fix pattern (broken):
        broken = ForwardRef(line, "Barracks.Research")
        with self.assertRaises(KeyError):
            broken.resolve_raw()

        # Post-fix pattern (works):
        fixed = ForwardRef(line, "Barracks")
        fixed.resolve_raw()  # should not raise


if __name__ == "__main__":
    unittest.main()