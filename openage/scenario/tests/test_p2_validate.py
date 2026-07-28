# Copyright 2026 the openage authors. See copying.md for legal info.
"""
P2.2 tests for the IR validator.

The validator enforces spec §6.2 invariants that the IR construction
checks cannot express on their own (they need cross-reference lookup):

  - TriggerCondition.target_scenario_object_id references a known
    EntityPlacement scenario_object_id.
  - TriggerCondition.target_area_id references a known AreaDefinition.
  - TriggerCondition.target_trigger_id references a known
    TriggerDefinition.
  - TriggerEffect.target_scenario_object_id references a known
    EntityPlacement scenario_object_id.
  - TriggerEffect.target_area_id references a known AreaDefinition.
  - TriggerEffect.target_trigger_id references a known
    TriggerDefinition.
  - The trigger activation graph is acyclic by default (cycle policy
    is strict for now; future relaxed policy is a follow-up).
  - Condition / effect variants supported by the validator pass;
    unsupported variants are reported, never silently accepted.
  - ``validate()`` returns a structured ``ValidationResult`` so the
    caller can distinguish ERROR vs WARNING vs OK.

The validator is pure-Python and does NOT depend on the nyan database.
Reference resolution is by string equality on the in-IR ``id`` fields.
"""
from __future__ import annotations

import pytest

from openage.scenario.ir import (
    AreaDefinition,
    EntityPlacement,
    InvalidIR,
    MapDefinition,
    PlayerDefinition,
    ScenarioDefinition,
    TriggerCondition,
    TriggerDefinition,
    TriggerEffect,
    VictoryDefinition,
)
from openage.scenario.validation import (
    ValidationResult,
    ValidationSeverity,
    validate,
)


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------


def _valid_map() -> MapDefinition:
    return MapDefinition(
        width=4, height=4,
        terrain_ids=["grass"] * 16,
        elevation=[0] * 16,
    )


def _player(pid: int) -> PlayerDefinition:
    return PlayerDefinition(player_id=pid, name=f"P{pid}", starting_resources=())


def _placement(sid: str, pid: int = 0) -> EntityPlacement:
    return EntityPlacement(
        scenario_object_id=sid,
        entity_definition_fqon="fqon.A",
        player_id=pid,
        position=(0, 0),
    )


def _trigger(
    tid: str,
    conditions: tuple = (),
    effects: tuple = (),
    initially_enabled: bool = True,
    looping: bool = False,
    delay: float = 0.0,
) -> TriggerDefinition:
    return TriggerDefinition(
        trigger_id=tid,
        initially_enabled=initially_enabled,
        looping=looping,
        delay=delay,
        conditions=conditions,
        effects=effects,
    )


def _scenario(
    placements: list[EntityPlacement],
    triggers: list[TriggerDefinition],
    areas: list[AreaDefinition] | None = None,
) -> ScenarioDefinition:
    return ScenarioDefinition(
        id="X",
        source="hand-authored",
        source_file="f.nyan",
        map=_valid_map(),
        players=[_player(0), _player(1)],
        placements=placements,
        areas=areas or [],
        triggers=triggers,
        victory=VictoryDefinition(kind="objective-driven", required_removals=()),
        produced_at_commit="fbb23fcfb6bb05a5ba0dccb8c860573e1e11b72e",
    )


# ---------------------------------------------------------------------------
# Clean scenarios
# ---------------------------------------------------------------------------


def test_validate_clean_scenario_with_no_triggers_returns_ok() -> None:
    s = _scenario(placements=[_placement("A")], triggers=[])
    result = validate(s)
    assert result.ok
    assert len(result.errors) == 0


def test_validate_clean_scenario_with_self_referential_safe_trigger() -> None:
    """A trigger that fires itself but only as a one-shot is allowed.

    The 'cyclic activation' rule is about LOOPING triggers that create
    a cycle; one-shot activations are fine.
    """
    cond = TriggerCondition(kind="TriggerFired", target_trigger_id="T0")
    eff = TriggerCondition(kind="ActivateTrigger", target_trigger_id="T0")
    t = _trigger("T0", conditions=(cond,), effects=(eff,))
    s = _scenario(placements=[_placement("A")], triggers=[t])
    result = validate(s)
    # Currently allow TriggerFired->ActivateTrigger chains but report
    # the same-id activate as INFO (it is a self-edge but not a loop).
    assert result.ok or result.has_warnings_only()


# ---------------------------------------------------------------------------
# Trigger reference resolution
# ---------------------------------------------------------------------------


def test_validate_rejects_condition_referencing_unknown_object() -> None:
    cond = TriggerCondition(kind="EntityDestroyed", target_scenario_object_id="Ghost")
    t = _trigger("T0", conditions=(cond,))
    s = _scenario(placements=[_placement("A")], triggers=[t])
    result = validate(s)
    assert not result.ok
    assert any("Ghost" in err.message for err in result.errors)


def test_validate_rejects_condition_referencing_unknown_area() -> None:
    cond = TriggerCondition(kind="EntityInArea", target_area_id="NoSuchArea")
    t = _trigger("T0", conditions=(cond,))
    s = _scenario(
        placements=[_placement("A")], triggers=[t],
        areas=[AreaDefinition(area_id="A0", kind="rect")],
    )
    result = validate(s)
    assert not result.ok
    assert any("NoSuchArea" in err.message for err in result.errors)


def test_validate_rejects_condition_referencing_unknown_trigger() -> None:
    cond = TriggerCondition(kind="TriggerFired", target_trigger_id="NoSuchTrigger")
    t = _trigger("T0", conditions=(cond,))
    s = _scenario(placements=[_placement("A")], triggers=[t])
    result = validate(s)
    assert not result.ok
    assert any("NoSuchTrigger" in err.message for err in result.errors)


def test_validate_rejects_effect_referencing_unknown_object() -> None:
    eff = TriggerEffect(kind="RemoveEntity", target_scenario_object_id="Phantom")
    t = _trigger("T0", effects=(eff,))
    s = _scenario(placements=[_placement("A")], triggers=[t])
    result = validate(s)
    assert not result.ok
    assert any("Phantom" in err.message for err in result.errors)


def test_validate_rejects_effect_referencing_unknown_trigger() -> None:
    eff = TriggerEffect(kind="ActivateTrigger", target_trigger_id="T404")
    t = _trigger("T0", effects=(eff,))
    s = _scenario(placements=[_placement("A")], triggers=[t])
    result = validate(s)
    assert not result.ok
    assert any("T404" in err.message for err in result.errors)


# ---------------------------------------------------------------------------
# Cyclic activation
# ---------------------------------------------------------------------------


def test_validate_rejects_looping_trigger_cycle() -> None:
    """Two looping triggers that activate each other form a cycle."""
    eff_a = TriggerEffect(kind="ActivateTrigger", target_trigger_id="T1")
    eff_b = TriggerEffect(kind="ActivateTrigger", target_trigger_id="T0")
    t_a = _trigger("T0", effects=(eff_a,), looping=True)
    t_b = _trigger("T1", effects=(eff_b,), looping=True)
    s = _scenario(placements=[_placement("A")], triggers=[t_a, t_b])
    result = validate(s)
    assert not result.ok
    assert any("cycle" in err.message.lower() for err in result.errors)


def test_validate_allows_looping_trigger_without_cycles() -> None:
    """A looping trigger that activates a one-shot trigger is fine."""
    eff = TriggerEffect(kind="ActivateTrigger", target_trigger_id="T1")
    t0 = _trigger("T0", effects=(eff,), looping=True)
    t1 = _trigger("T1")  # one-shot, no effects
    s = _scenario(placements=[_placement("A")], triggers=[t0, t1])
    result = validate(s)
    assert result.ok


# ---------------------------------------------------------------------------
# Unsupported variants
# ---------------------------------------------------------------------------


def test_validate_rejects_unsupported_condition_kind() -> None:
    cond = TriggerCondition(kind="ThisKindDoesNotExist", target_scenario_object_id="A")
    t = _trigger("T0", conditions=(cond,))
    s = _scenario(placements=[_placement("A")], triggers=[t])
    result = validate(s)
    assert not result.ok
    assert any("ThisKindDoesNotExist" in err.message for err in result.errors)


def test_validate_rejects_unsupported_effect_kind() -> None:
    eff = TriggerEffect(kind="MakeItRain", target_scenario_object_id="A")
    t = _trigger("T0", effects=(eff,))
    s = _scenario(placements=[_placement("A")], triggers=[t])
    result = validate(s)
    assert not result.ok
    assert any("MakeItRain" in err.message for err in result.errors)


# ---------------------------------------------------------------------------
# ValidationResult shape
# ---------------------------------------------------------------------------


def test_validation_result_distinguishes_severities() -> None:
    s = _scenario(placements=[_placement("A")], triggers=[])
    result = validate(s)
    assert isinstance(result, ValidationResult)
    assert all(e.severity == ValidationSeverity.ERROR for e in result.errors)
    assert all(w.severity == ValidationSeverity.WARNING for w in result.warnings)


def test_validation_result_ok_is_false_when_errors_exist() -> None:
    eff = TriggerEffect(kind="UnknownEffect", target_scenario_object_id="A")
    t = _trigger("T0", effects=(eff,))
    s = _scenario(placements=[_placement("A")], triggers=[t])
    result = validate(s)
    assert not result.ok
    assert result.has_errors()


# ---------------------------------------------------------------------------
# Self-consistency with IR construction
# ---------------------------------------------------------------------------


def test_ir_construction_catches_duplicate_ids_so_validator_is_not_asked() -> None:
    """The IR's __post_init__ already rejects duplicate scenario_object_ids;
    we document that contract here as a regression guard."""
    with pytest.raises(InvalidIR, match="duplicate scenario_object_id"):
        _scenario(
            placements=[_placement("Dup"), _placement("Dup")],
            triggers=[],
        )