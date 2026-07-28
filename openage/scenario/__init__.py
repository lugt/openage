"""
Scenario IR + extraction support.

This package is the home for the Mission 1 Scenario IR loader (P2) and
any source-specific extractor (Path C of the WW_MISSION1 plan) that
may be added in the future if the Path D BLOCKER documented in
``docs/mvp/reports/mission1-scenario-feasibility.md`` is lifted.

Current contents:
  - ``dumps`` module: loaders and validators for the P1 feasibility
    dump artifacts under ``docs/mvp/reports/mission1-scenario-dump/``.
  - ``tests``: pytest suite validating the dump artifacts.
"""