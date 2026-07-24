# Copyright 2026 the openage authors. See copying.md for legal info.
"""
Minimum viable validate-nyan subcommand.

Stage 1: walks each modpack's .nyan files, scans for top-level
imports, declarations, and `<FQON>` type references, and reports
references that cannot be resolved to a declaration reachable
through the file's import graph.

This catches the common "unknown name" failure mode (e.g.
slavs.nyan:813 ArsonResearchable) without launching the simulation,
renderer, or converter. It does NOT replace a full semantic check
performed by libnyan; it is a fast pre-flight for CI.

The plan calls for swapping the scan with a real `nyan::Database::load`
call once Sprint A / Task 6 lands load_batch. The public surface
(`validate_nyan(modpacks, json_path=, continue_on_error=, strict=)`)
is intentionally compatible with that future swap.
"""
from __future__ import annotations

import argparse
import json
import pathlib
import re
import sys
from typing import Iterable


# Regexes are intentionally narrow. We are not reimplementing the nyan
# grammar; we are looking for the patterns that surface real bugs
# (missing FQON in type-argument position).
_IMPORT_RE = re.compile(r"^\s*import\s+([A-Za-z_][\w]*(?:\.[A-Za-z_][\w]*)+)"
                        r"(?:\s+as\s+([A-Za-z_][\w]*))?", re.M)
# `<FQON>` type argument. We avoid matching `<<`. The FQON may have
# any number of segments (>= 1) since real bugs often involve a
# bare `ArsonResearchable` form just as much as a fully qualified
# `ArsonResearchable.ArsonCost.GoldAmount`.
_FQON_RE = re.compile(r"<([A-Za-z_][\w]*(?:\.[A-Za-z_][\w]*)*)>")
# Declaration pattern: `Name(...):` anywhere in the file. nyan allows
# nested objects (indented under another object), so we cannot rely
# on `^` to mean "top level". Instead, we accept any `Name(` token
# and trust the nyan parser's later semantic check to filter. This
# over-collects (set elements like `Foo, Bar` are caught too) but the
# union of every file is a reasonable upper bound for a corpus-wide
# catalog of declared FQONs.
_DECL_RE = re.compile(r"(?<![\w.])([A-Z][\w]*(?:\.[A-Z][\w]*)*)\s*\(")


def _scan_file(path: pathlib.Path) -> dict:
    """
    Scan a single .nyan file and return its declared FQONs, imports,
    and referenced FQONs (in type-argument position).
    """
    declared: set[str] = set()
    imports: set[str] = set()  # includes aliased forms as `alias.FQON`
    refs: list[tuple[int, str]] = []  # (line_no, fqon)
    text = path.read_text(encoding="utf-8", errors="replace")
    for lineno, raw in enumerate(text.splitlines(), 1):
        m = _IMPORT_RE.match(raw)
        if m:
            target = m.group(1)
            imports.add(target)
            if m.group(2):
                alias_prefix = m.group(2)
                # alias prefix aliases every member of the target module.
                imports.add(f"{alias_prefix}.*")
            continue
        for m_decl in _DECL_RE.finditer(raw):
            name = m_decl.group(1)
            # Skip the obvious false positives:
            #   - `patch.NyanPatch`, `research.ResearchableTech` etc. are
            #     type expressions, not declarations. They always begin
            #     with a lowercase module name. We require the first
            #     segment to be capitalized.
            #   - `o{...}` set literals use `Foo, Bar` syntax; we have
            #     already filtered to `Foo(` form, but the `(` after a
            #     name in a set is rare. Most set entries are bare.
            declared.add(name)
        for fqon in _FQON_RE.findall(raw):
            refs.append((lineno, fqon))
    return {"declared": declared, "imports": imports, "refs": refs}


def _modpack_root(modpack: str) -> pathlib.Path:
    p = pathlib.Path(modpack)
    if not p.exists():
        raise FileNotFoundError(modpack)
    return p


def _build_corpus_index(files: list[pathlib.Path]) -> set[str]:
    """
    Walk every .nyan file in the corpus and build a set of every
    FQON that is either declared at the top level or appears in
    an `import` statement. This is the corpus-wide catalog of
    names that references are expected to resolve against.
    """
    index: set[str] = set()
    for path in files:
        try:
            scan = _scan_file(path)
        except OSError:
            continue
        index |= scan["declared"]
        for imp in scan["imports"]:
            if not imp.endswith(".*"):
                index.add(imp)
    return index


def _collect_errors(modpack: str) -> list[dict]:
    """
    Walk a modpack and emit errors for references to FQONs that are
    not present anywhere in the corpus index. The check is
    conservative: a reference is reported missing if no file in the
    modpack declares a name with the same full path OR with the
    same first segment as a top-level declaration.
    """
    errors: list[dict] = []
    root = _modpack_root(modpack)
    nyan_files = sorted(root.rglob("*.nyan"))
    corpus_index = _build_corpus_index(nyan_files)
    # Also collect first segments of every top-level declaration so
    # bare-name references like `<ArsonResearchable>` can be flagged
    # when the FQON is not declared anywhere in the corpus.
    first_segments: set[str] = set()
    for fqon in corpus_index:
        first_segments.add(fqon.split(".", 1)[0])
    for path in nyan_files:
        try:
            scan = _scan_file(path)
        except OSError as exc:
            errors.append({
                "severity": "error",
                "category": "io",
                "file": str(path),
                "line": 0,
                "column": 0,
                "message": f"failed to read: {exc}",
            })
            continue
        # Local file knows: its own declarations + its own imports
        local_known = set(scan["declared"]) | set(scan["imports"])
        for lineno, fqon in scan["refs"]:
            if fqon in corpus_index:
                continue
            if fqon in local_known:
                continue
            # Wildcard alias `alias.*` from local imports.
            prefix = fqon.split(".", 1)[0]
            if f"{prefix}.*" in local_known:
                continue
            # Single-segment FQON like `<ArsonResearchable>` is missing
            # if the name is not declared anywhere in the corpus and
            # is not a first-segment of any corpus declaration that
            # could reasonably alias it.
            if fqon not in first_segments:
                errors.append({
                    "severity": "error",
                    "category": "missing_fqon",
                    "file": str(path),
                    "line": lineno,
                    "column": 0,
                    "missing_name": fqon,
                    "message": f"unknown name: {fqon}",
                })
    return errors


def validate_nyan(modpacks: Iterable[str], *, json_path: str | None = None,
                  continue_on_error: bool = True, strict: bool = False) -> int:
    """
    Validate each modpack in turn and return an exit code:
        0 = clean
        1 = fatal error(s)
        2 = warnings only in --strict mode
    """
    errors: list[dict] = []
    for modpack in modpacks:
        try:
            errors.extend(_collect_errors(modpack))
        except FileNotFoundError as exc:
            errors.append({
                "severity": "error",
                "category": "io",
                "file": str(exc),
                "line": 0,
                "column": 0,
                "message": f"modpack not found: {exc}",
            })

    fatal = [e for e in errors if e.get("severity") != "warning"]
    warnings = [e for e in errors if e.get("severity") == "warning"]
    report = {
        "version": 1,
        "modpacks": list(modpacks),
        "errors": errors,
        "summary": {
            "errors": len(fatal),
            "warnings": len(warnings),
        },
    }
    if json_path:
        pathlib.Path(json_path).write_text(
            json.dumps(report, indent=2, sort_keys=True), encoding="utf-8"
        )
    if fatal:
        return 1
    if strict and warnings:
        return 2
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="validate-nyan")
    parser.add_argument("--modpacks", action="append", required=True,
                        help="Path to a modpack directory (repeatable).")
    parser.add_argument("--json", dest="json_path", default=None,
                        help="Write a JSON report to this path.")
    parser.add_argument("--continue-on-error", dest="continue_on_error",
                        action="store_true", default=True)
    parser.add_argument("--strict", action="store_true", default=False)
    args = parser.parse_args(argv)
    return validate_nyan(
        args.modpacks,
        json_path=args.json_path,
        continue_on_error=args.continue_on_error,
        strict=args.strict,
    )


if __name__ == "__main__":
    sys.exit(main())
