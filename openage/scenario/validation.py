"""
IR validator (P2.2).

Cross-reference validation for a :class:`ScenarioDefinition`. The IR
construction already enforces the per-type invariants (unique IDs,
dense arrays, valid positions); this module adds the cross-cutting
checks that need to look at the whole scenario:

  - Trigger conditions / effects reference known ScenarioObjectIds,
    area_ids, trigger_ids.
  - Supported condition / effect variants per spec §7.
  - Looping triggers form no activation cycle.
  - Validation issues are reported with a stable severity so the
    caller (loader, CI, dev-mode startup) can decide whether to
    abort or warn.

The validator is pure-Python; it does not depend on the nyan
database or the C++ runtime. Reference resolution is by string
equality on the in-IR ``id`` fields.
"""
from __future__ import annotations

import dataclasses
import enum
from typing import Iterable

from openage.scenario.ir import (
    ScenarioDefinition,
    TriggerCondition,
    TriggerDefinition,
    TriggerEffect,
)


# ---------------------------------------------------------------------------
# Severity + result
# ---------------------------------------------------------------------------


class ValidationSeverity(enum.Enum):
    ERROR = "error"
    WARNING = "warning"
    INFO = "info"


@dataclasses.dataclass(frozen=True)
class ValidationIssue:
    severity: ValidationSeverity
    code: str
    message: str
    location: str = ""

    def __str__(self) -> str:
        prefix = self.location or "<scenario>"
        return f"[{self.severity.value}] {prefix}: {self.code}: {self.message}"


@dataclasses.dataclass(frozen=True)
class ValidationResult:
    errors: tuple[ValidationIssue, ...] = ()
    warnings: tuple[ValidationIssue, ...] = ()
    infos: tuple[ValidationIssue, ...] = ()

    @property
    def ok(self) -> bool:
        return not self.errors

    def has_errors(self) -> bool:
        return bool(self.errors)

    def has_warnings_only(self) -> bool:
        return not self.errors and bool(self.warnings)

    def all_issues(self) -> tuple[ValidationIssue, ...]:
        return self.errors + self.warnings + self.infos


# ---------------------------------------------------------------------------
# Supported variants (spec §7 candidates, narrowed for the MVP)
# ---------------------------------------------------------------------------


# Condition kinds the validator accepts. Unknown kinds are rejected.
# Keep in sync with spec §7 + the plan §P4 candidate list.
SUPPORTED_CONDITION_KINDS: frozenset[str] = frozenset({
    "AoE2Condition",
    "TimerElapsed",
    "EntityInArea",
    "EntityDestroyed",
    "EntityVisitedArea",
    "EntityAlive",
    "EntityOwnedBy",
    "PlayerOwnsCount",
    "AttributeCompare",
    "TriggerFired",
})

# Effect kinds the validator accepts.
SUPPORTED_EFFECT_KINDS: frozenset[str] = frozenset({
    "AoE2Effect",
    "DisplayInstruction",
    "SetObjective",
    "CompleteObjective",
    "IssueMoveCommand",
    "IssueAttackCommand",
    "ActivateTrigger",
    "DeactivateTrigger",
    "CreateEntity",
    "RemoveEntity",
    "ChangeOwnership",
    "DeclareVictory",
    "DeclareDefeat",
})


# ---------------------------------------------------------------------------
# Helper
# ---------------------------------------------------------------------------


def _err(code: str, message: str, location: str = "") -> ValidationIssue:
    return ValidationIssue(
        severity=ValidationSeverity.ERROR, code=code, message=message, location=location,
    )


def _warn(code: str, message: str, location: str = "") -> ValidationIssue:
    return ValidationIssue(
        severity=ValidationSeverity.WARNING, code=code, message=message, location=location,
    )


def _info(code: str, message: str, location: str = "") -> ValidationIssue:
    return ValidationIssue(
        severity=ValidationSeverity.INFO, code=code, message=message, location=location,
    )


# ---------------------------------------------------------------------------
# Reference resolution
# ---------------------------------------------------------------------------


def _collect_known_ids(
    scenario: ScenarioDefinition,
) -> tuple[set[str], set[str], set[str]]:
    obj_ids = {p.scenario_object_id for p in scenario.placements}
    area_ids = {a.area_id for a in scenario.areas}
    trigger_ids = {t.trigger_id for t in scenario.triggers}
    return obj_ids, area_ids, trigger_ids


def _check_references(
    issues: list[ValidationIssue],
    *,
    target_scenario_object_id: str | None,
    target_area_id: str | None,
    target_trigger_id: str | None,
    known_objs: set[str],
    known_areas: set[str],
    known_triggers: set[str],
    location: str,
) -> None:
    if target_scenario_object_id is not None \
            and target_scenario_object_id not in known_objs:
        issues.append(_err(
            "unknown_scenario_object_id",
            f"references unknown scenario_object_id {target_scenario_object_id!r}",
            location=location,
        ))
    if target_area_id is not None and target_area_id not in known_areas:
        issues.append(_err(
            "unknown_area_id",
            f"references unknown area_id {target_area_id!r}",
            location=location,
        ))
    if target_trigger_id is not None \
            and target_trigger_id not in known_triggers:
        issues.append(_err(
            "unknown_trigger_id",
            f"references unknown trigger_id {target_trigger_id!r}",
            location=location,
        ))


def _check_condition_kind(
    issues: list[ValidationIssue],
    cond: TriggerCondition,
    location: str,
) -> None:
    if cond.kind not in SUPPORTED_CONDITION_KINDS:
        issues.append(_err(
            "unsupported_condition_kind",
            f"condition kind {cond.kind!r} is not in the supported set "
            f"{sorted(SUPPORTED_CONDITION_KINDS)}",
            location=location,
        ))


def _check_effect_kind(
    issues: list[ValidationIssue],
    eff: TriggerEffect,
    location: str,
) -> None:
    if eff.kind not in SUPPORTED_EFFECT_KINDS:
        issues.append(_err(
            "unsupported_effect_kind",
            f"effect kind {eff.kind!r} is not in the supported set "
            f"{sorted(SUPPORTED_EFFECT_KINDS)}",
            location=location,
        ))


def _walk_condition_args(
    cond: TriggerCondition,
) -> Iterable[tuple[str | None, str | None, str | None]]:
    yield cond.target_scenario_object_id, cond.target_area_id, cond.target_trigger_id


def _walk_effect_args(
    eff: TriggerEffect,
) -> Iterable[tuple[str | None, str | None, str | None]]:
    yield eff.target_scenario_object_id, eff.target_area_id, eff.target_trigger_id


# ---------------------------------------------------------------------------
# Cycle detection (looping triggers only)
# ---------------------------------------------------------------------------


def _detect_activation_cycles(
    triggers: list[TriggerDefinition],
) -> list[tuple[str, ...]]:
    """Return the list of cycles in the LOOPING-trigger activation graph.

    A cycle is reported as a tuple of trigger_ids in the order they
    appear in the DFS walk. Non-looping triggers that activate other
    triggers do NOT contribute to the cycle graph.
    """
    by_id = {t.trigger_id: t for t in triggers}
    # Edges only between looping triggers
    adj: dict[str, list[str]] = {tid: [] for tid in by_id}
    for tid, t in by_id.items():
        if not t.looping:
            continue
        for eff in t.effects:
            if not isinstance(eff, TriggerEffect):
                continue
            if eff.kind != "ActivateTrigger":
                continue
            target = eff.target_trigger_id
            if target is None or target not in by_id:
                continue
            if not by_id[target].looping:
                continue
            adj[tid].append(target)

    cycles: list[tuple[str, ...]] = []
    WHITE, GRAY, BLACK = 0, 1, 2
    color = {tid: WHITE for tid in by_id}
    stack: list[str] = []

    def dfs(node: str) -> None:
        color[node] = GRAY
        stack.append(node)
        for nxt in adj[node]:
            if color[nxt] == GRAY:
                # Found a back-edge -> cycle.
                idx = stack.index(nxt)
                cycles.append(tuple(stack[idx:] + [nxt]))
            elif color[nxt] == WHITE:
                dfs(nxt)
        stack.pop()
        color[node] = BLACK

    for tid in by_id:
        if color[tid] == WHITE:
            dfs(tid)
    return cycles


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------


def validate(scenario: ScenarioDefinition) -> ValidationResult:
    """Validate a ScenarioDefinition.

    The function is pure: it does not raise on errors, it returns a
    :class:`ValidationResult` that the caller can inspect. IR-layer
    construction errors are raised separately by the dataclass
    ``__post_init__``; this function only adds cross-cutting checks.
    """
    errors: list[ValidationIssue] = []
    warnings: list[ValidationIssue] = []
    infos: list[ValidationIssue] = []

    known_objs, known_areas, known_triggers = _collect_known_ids(scenario)

    for trigger in scenario.triggers:
        loc = f"trigger[{trigger.trigger_id}]"

        for cond in trigger.conditions:
            if not isinstance(cond, TriggerCondition):
                continue
            _check_condition_kind(errors, cond, location=f"{loc}.condition[{cond.kind}]")
            obj_id, area_id, trig_id = next(_walk_condition_args(cond))
            _check_references(
                errors,
                target_scenario_object_id=obj_id,
                target_area_id=area_id,
                target_trigger_id=trig_id,
                known_objs=known_objs,
                known_areas=known_areas,
                known_triggers=known_triggers,
                location=f"{loc}.condition[{cond.kind}]",
            )

        for eff in trigger.effects:
            if not isinstance(eff, TriggerEffect):
                continue
            _check_effect_kind(errors, eff, location=f"{loc}.effect[{eff.kind}]")
            obj_id, area_id, trig_id = next(_walk_effect_args(eff))
            if eff.kind in {"SetObjective", "CompleteObjective", "DeclareVictory"}:
                obj_id, area_id = None, None
            _check_references(
                errors,
                target_scenario_object_id=obj_id,
                target_area_id=area_id,
                target_trigger_id=trig_id,
                known_objs=known_objs,
                known_areas=known_areas,
                known_triggers=known_triggers,
                location=f"{loc}.effect[{eff.kind}]",
            )

        # Self-edge warning: a trigger that activates itself. Allowed but
        # easy to typo, so report as INFO.
        for eff in trigger.effects:
            if isinstance(eff, TriggerEffect) \
                    and eff.kind == "ActivateTrigger" \
                    and eff.target_trigger_id == trigger.trigger_id:
                infos.append(_info(
                    "self_activation",
                    f"trigger {trigger.trigger_id!r} activates itself",
                    location=loc,
                ))

    # Cycle detection (looping triggers only).
    for cycle in _detect_activation_cycles(scenario.triggers):
        errors.append(_err(
            "looping_trigger_cycle",
            f"looping trigger activation cycle: {' -> '.join(cycle)}",
            location="<triggers>",
        ))

    # Areas that are not referenced by any condition/effect are reported
    # as WARNING (informational; not blocking).
    referenced_areas: set[str] = set()
    for trigger in scenario.triggers:
        for cond in trigger.conditions:
            if isinstance(cond, TriggerCondition) and cond.target_area_id is not None:
                referenced_areas.add(cond.target_area_id)
        for eff in trigger.effects:
            if isinstance(eff, TriggerEffect) and eff.target_area_id is not None:
                referenced_areas.add(eff.target_area_id)
    for area in scenario.areas:
        if area.area_id not in referenced_areas:
            warnings.append(_warn(
                "unused_area",
                f"area {area.area_id!r} is defined but never referenced",
                location=f"area[{area.area_id}]",
            ))

    return ValidationResult(
        errors=tuple(errors),
        warnings=tuple(warnings),
        infos=tuple(infos),
    )