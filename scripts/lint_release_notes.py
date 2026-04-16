#!/usr/bin/env python3
"""Release-notes linter — blocks releases whose notes don't match the shipped assets.

Purpose
-------
The pre-release v0.7.4 shipped release notes that named a DirectML installer
binary that was never uploaded as a release asset, plus mentions of AMD / Intel
/ Windows ML support tracks that the project does not actually ship. Users
downloaded based on promises that didn't match reality. This linter exists to
make that class of release impossible to ship again.

The linter is intentionally strict. It fails fast and loudly — it is wired into
CI tiers 2 and 3 as a hard block.

Rules
-----
1. ASSET-EXISTENCE (hard fail)
   Every filename of the form `CorridorKey_*.{exe,zip,dmg,pkg,tar,tar.gz,tgz,msi}`
   mentioned in the release notes MUST correspond to a real packaged asset.
   The set of "real" assets is supplied via --assets-file (CI) or --release-tag
   (post-release verification against the GitHub release).

2. SCOPE (hard fail)
   A configurable list of backend tracks is "forbidden unless at least one
   asset file name mentions that backend." The default forbidden list reflects
   the current project scope: RTX + Apple Silicon only. Mentioning DirectML /
   Windows ML / WinML / AMD GPU / Intel Arc / Vulkan triggers a failure unless
   an asset filename contains that token.

   Matches happen on whole-word, case-insensitive strings anchored to known
   product-claim verbs (download, run on, support for, track, installer) to
   cut false positives from narrative prose. Comment lines ("Do not describe
   the ... installer as ...") are skipped because they're disclaimers, not
   claims.

Usage
-----
   # CI Tier 2: validate against packaged artifacts on disk
   python3 scripts/lint_release_notes.py release_notes.md \
       --assets-file dist/asset_manifest.txt

   # CI Tier 3: validate against the actual GitHub release
   python3 scripts/lint_release_notes.py release_notes.md \
       --release-tag v0.7.5

   # Dry-run / explain mode (no exit code change)
   python3 scripts/lint_release_notes.py release_notes.md \
       --assets-file dist/asset_manifest.txt --warn-only

Assets-file format: one filename per line, blank lines and `#` comments ignored.

Exit codes:
   0 = clean
   1 = violations found (or --warn-only and violations found)
   2 = invocation error (bad args, missing file)
"""
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable

# --- Configuration ------------------------------------------------------------

# Asset filename pattern. Kept conservative — match the project's actual
# release artifact naming so prose about unrelated binaries (e.g. Resolve.app)
# doesn't trip the linter.
ASSET_FILENAME_PATTERN = re.compile(
    r"""
    \b                                  # word boundary
    CorridorKey[_\-][\w\.\-]+           # project prefix
    \.(?:exe|zip|dmg|pkg|msi|tar\.gz|tgz|tar|deb|rpm)   # known extensions
    \b
    """,
    re.IGNORECASE | re.VERBOSE,
)

# Forbidden backend tokens. A release-note line that names one of these in a
# product-claim context (see CLAIM_VERBS) fails unless an asset filename
# contains the same token. Tokens are case-insensitive whole-word matches.
#
# This is the guardrail that prevents "We ship DirectML support" when no
# DirectML asset is in the build.
FORBIDDEN_BACKENDS_DEFAULT: list[str] = [
    "DirectML",
    "Windows ML",
    "WinML",
    "AMD GPU",
    "Intel Arc",
    "Intel GPU",
    "Vulkan",
]

# Verbs / phrases that indicate a product claim rather than incidental mention.
# Line must contain one of these near the backend token to trigger.
CLAIM_CONTEXT_PATTERN = re.compile(
    r"""
    \b(?:
        download | run\s+on | runs\s+on | support(?:s|ed)?\s+for |
        support(?:s|ed)?  | track | installer | backend | build |
        available\s+on    | shipped\s+with | available\s+for
    )\b
    """,
    re.IGNORECASE | re.VERBOSE,
)

# Lines starting with these markers are treated as disclaimers, not claims.
DISCLAIMER_PREFIXES = (
    "do not describe",
    "this release does not",
    "note:",
    "disclaimer:",
)


# --- Data types ---------------------------------------------------------------


@dataclass
class Violation:
    rule: str
    line_number: int
    message: str
    context: str = ""

    def format(self) -> str:
        ctx = f"\n    > {self.context.strip()}" if self.context else ""
        return f"[{self.rule}] {Path(self.message).name or self.message} (line {self.line_number}){ctx}"


@dataclass
class LintReport:
    violations: list[Violation] = field(default_factory=list)
    notes_path: Path | None = None
    assets_checked: list[str] = field(default_factory=list)

    def add(self, v: Violation) -> None:
        self.violations.append(v)

    def ok(self) -> bool:
        return not self.violations

    def render(self) -> str:
        if self.ok():
            n = len(self.assets_checked)
            return f"release-notes linter: OK ({n} asset(s) verified against notes)"
        out = [f"release-notes linter: {len(self.violations)} violation(s) in {self.notes_path}"]
        for v in self.violations:
            out.append("  " + v.format())
        out.append("")
        out.append("See scripts/lint_release_notes.py for the rules that fired.")
        return "\n".join(out)


# --- IO helpers ---------------------------------------------------------------


def read_assets_file(path: Path) -> list[str]:
    assets: list[str] = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        assets.append(Path(line).name)
    if not assets:
        raise SystemExit(f"error: {path} contains no asset filenames")
    return assets


def read_assets_from_release(tag: str) -> list[str]:
    """Query GitHub for the assets attached to a release tag via gh CLI."""
    try:
        result = subprocess.run(
            ["gh", "release", "view", tag, "--json", "assets"],
            check=True,
            capture_output=True,
            text=True,
        )
    except FileNotFoundError:
        raise SystemExit("error: gh CLI not found; install it or use --assets-file")
    except subprocess.CalledProcessError as e:
        raise SystemExit(f"error: gh release view {tag} failed: {e.stderr.strip()}")
    payload = json.loads(result.stdout)
    return [a["name"] for a in payload.get("assets", [])]


# --- Linting rules ------------------------------------------------------------


def find_named_assets(notes: str) -> list[tuple[int, str, str]]:
    """Return (line_number, filename, line) for each asset filename in notes."""
    hits: list[tuple[int, str, str]] = []
    for idx, line in enumerate(notes.splitlines(), start=1):
        for match in ASSET_FILENAME_PATTERN.finditer(line):
            hits.append((idx, match.group(0), line))
    return hits


def check_asset_existence(
    notes: str, available_assets: Iterable[str], report: LintReport
) -> None:
    available_lower = {a.lower() for a in available_assets}
    for line_no, filename, line in find_named_assets(notes):
        if filename.lower() in available_lower:
            continue
        report.add(
            Violation(
                rule="asset-existence",
                line_number=line_no,
                message=(
                    f"{filename!r} is named in release notes but is not a real asset. "
                    "Either upload it, or remove the mention."
                ),
                context=line,
            )
        )


def check_scope(
    notes: str,
    available_assets: Iterable[str],
    forbidden: Iterable[str],
    report: LintReport,
) -> None:
    assets_joined = " ".join(available_assets).lower()
    for idx, raw_line in enumerate(notes.splitlines(), start=1):
        line = raw_line.strip()
        line_lower = line.lower()

        # Skip disclaimers — they explicitly negate a claim, so flagging them
        # would force authors to remove the safety net too.
        stripped_for_prefix = line_lower.lstrip("-*> ").strip()
        if any(stripped_for_prefix.startswith(p) for p in DISCLAIMER_PREFIXES):
            continue

        # Only trigger inside a product-claim context.
        if not CLAIM_CONTEXT_PATTERN.search(line):
            continue

        for token in forbidden:
            token_lower = token.lower()
            # Whole-word match. re.escape to handle tokens with spaces.
            word_pattern = re.compile(
                r"\b" + re.escape(token_lower) + r"\b", re.IGNORECASE
            )
            if not word_pattern.search(line_lower):
                continue
            # If any asset filename contains this token, the claim is backed by
            # an actual artifact and we let it through.
            asset_token = token_lower.replace(" ", "")
            if asset_token in assets_joined.replace(" ", ""):
                continue
            report.add(
                Violation(
                    rule="scope",
                    line_number=idx,
                    message=(
                        f"release notes claim {token!r} support but no asset ships that "
                        "track. Either add the asset, or remove the claim. "
                        "Scope for this project is currently RTX + Apple Silicon."
                    ),
                    context=raw_line,
                )
            )
    return


# --- Entry point --------------------------------------------------------------


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Lint release notes against the assets that will actually ship."
    )
    parser.add_argument("notes", type=Path, help="Path to release notes markdown")
    src = parser.add_mutually_exclusive_group(required=True)
    src.add_argument(
        "--assets-file",
        type=Path,
        help="Text file with one real asset filename per line",
    )
    src.add_argument(
        "--release-tag",
        help="GitHub release tag to query for real assets (via gh CLI)",
    )
    parser.add_argument(
        "--forbidden",
        action="append",
        default=None,
        help="Override forbidden-backend token list (can be given multiple times)",
    )
    parser.add_argument(
        "--warn-only",
        action="store_true",
        help="Print violations but exit 0",
    )
    args = parser.parse_args(argv)

    if not args.notes.is_file():
        print(f"error: notes file not found: {args.notes}", file=sys.stderr)
        return 2

    notes_text = args.notes.read_text(encoding="utf-8")
    available_assets = (
        read_assets_file(args.assets_file)
        if args.assets_file
        else read_assets_from_release(args.release_tag)
    )

    forbidden = args.forbidden if args.forbidden is not None else FORBIDDEN_BACKENDS_DEFAULT

    report = LintReport(notes_path=args.notes, assets_checked=list(available_assets))
    check_asset_existence(notes_text, available_assets, report)
    check_scope(notes_text, available_assets, forbidden, report)

    print(report.render())
    if report.ok():
        return 0
    return 0 if args.warn_only else 1


if __name__ == "__main__":
    raise SystemExit(main())
