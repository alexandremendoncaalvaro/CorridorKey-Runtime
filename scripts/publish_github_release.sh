#!/bin/bash
# Shared GitHub release publishing helper for the macOS and Linux pipelines.
#
# Mirrors the guardrails enforced by Publish-CorridorKeyGithubRelease in
# scripts/release_pipeline_windows.ps1: the operator must hand in a release
# notes file that already exists and carries every section required by
# docs/RELEASE_GUIDELINES.md section 5, the target tag must not already
# exist on the remote (tag immutability), every declared asset must be on
# disk, and the working tree must be clean. The helper only wraps the
# mechanical part (tag derivation, title format, --prerelease vs --latest
# flag, asset upload); it never composes release content.
#
# Usage:
#   scripts/publish_github_release.sh \
#     --platform mac \
#     --version 0.8.1 \
#     --display-label 0.8.1-mac.1 \
#     --notes-file build/release_notes/v0.8.1-mac.1.md \
#     --asset dist/CorridorKey_Resolve_v0.8.1-mac.1_macOS_AppleSilicon.dmg \
#     [--asset dist/<second asset>] \
#     [--repo alexandremendoncaalvaro/CorridorKey-Runtime] \
#     [--dry-run]
#
# When --display-label is omitted the tag is vX.Y.Z (stable) and the
# release is marked --latest; otherwise the tag is v<display-label> and
# the release is marked --prerelease. The label shape must be
# X.Y.Z-<platform>.N and the X.Y.Z portion must match --version.

set -euo pipefail

PLATFORM=""
VERSION=""
DISPLAY_LABEL=""
NOTES_FILE=""
REPO="alexandremendoncaalvaro/CorridorKey-Runtime"
DRY_RUN=0
ASSETS=()

usage() {
    sed -n '1,30p' "$0" >&2
    exit 2
}

while [ $# -gt 0 ]; do
    case "$1" in
        --platform) PLATFORM="$2"; shift 2 ;;
        --version) VERSION="$2"; shift 2 ;;
        --display-label) DISPLAY_LABEL="$2"; shift 2 ;;
        --notes-file) NOTES_FILE="$2"; shift 2 ;;
        --asset) ASSETS+=("$2"); shift 2 ;;
        --repo) REPO="$2"; shift 2 ;;
        --dry-run) DRY_RUN=1; shift ;;
        -h|--help) usage ;;
        *) echo "ERROR: unknown argument '$1'" >&2; usage ;;
    esac
done

case "$PLATFORM" in
    mac|linux|win) ;;
    "") echo "ERROR: --platform is required (mac|linux|win)" >&2; exit 2 ;;
    *) echo "ERROR: unsupported --platform '$PLATFORM'" >&2; exit 2 ;;
esac

if [ -z "$VERSION" ]; then
    echo "ERROR: --version X.Y.Z is required" >&2
    exit 2
fi

if ! [[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "ERROR: --version '$VERSION' is not SemVer X.Y.Z" >&2
    exit 2
fi

if [ -n "$DISPLAY_LABEL" ]; then
    if ! [[ "$DISPLAY_LABEL" =~ ^([0-9]+\.[0-9]+\.[0-9]+)-${PLATFORM}\.([0-9]+)$ ]]; then
        echo "ERROR: --display-label '$DISPLAY_LABEL' is not a valid ${PLATFORM} prerelease label." >&2
        echo "       Expected form: X.Y.Z-${PLATFORM}.N (see docs/RELEASE_GUIDELINES.md section 1)." >&2
        exit 2
    fi
    label_core="${BASH_REMATCH[1]}"
    if [ "$label_core" != "$VERSION" ]; then
        echo "ERROR: --display-label core '$label_core' does not match --version '$VERSION'." >&2
        echo "       The label must be '$VERSION-${PLATFORM}.<counter>'." >&2
        exit 2
    fi
fi

if [ ${#ASSETS[@]} -eq 0 ]; then
    echo "ERROR: at least one --asset <path> is required" >&2
    exit 2
fi

if [ -z "$NOTES_FILE" ]; then
    echo "ERROR: --notes-file is required. Write build/release_notes/v<tag>.md per docs/RELEASE_GUIDELINES.md section 5." >&2
    exit 2
fi

if [ ! -f "$NOTES_FILE" ]; then
    echo "ERROR: release notes file not found at '$NOTES_FILE'." >&2
    exit 2
fi

if [ ! -s "$NOTES_FILE" ]; then
    echo "ERROR: release notes file '$NOTES_FILE' is empty." >&2
    exit 2
fi

for marker in '## Overview' '## Changelog' '## Assets & Downloads' '## Installation Instructions'; do
    if ! grep -Fq "$marker" "$NOTES_FILE"; then
        echo "ERROR: notes file '$NOTES_FILE' is missing required section '$marker'." >&2
        echo "       See docs/RELEASE_GUIDELINES.md section 5 for the required template." >&2
        exit 2
    fi
done

if ! command -v gh >/dev/null 2>&1; then
    echo "ERROR: 'gh' CLI is not on PATH. Install GitHub CLI or publish manually." >&2
    exit 2
fi

if ! command -v git >/dev/null 2>&1; then
    echo "ERROR: 'git' is not on PATH." >&2
    exit 2
fi

# Dirty-tree rule (absolute): the pipeline refuses to publish when
# `git describe --dirty` reports -dirty, or when there are staged but
# uncommitted changes. This closes the loophole where a label baked from
# an uncommitted working tree could reach users and be unreproducible
# from Git. See docs/RELEASE_GUIDELINES.md section 1.
if ! git diff --quiet || ! git diff --cached --quiet; then
    echo "ERROR: working tree has uncommitted changes. Refusing to publish." >&2
    echo "       Commit or stash your changes, then retry." >&2
    git status --short >&2
    exit 2
fi

# Also reject untracked files under tracked scopes that would be lost
# from the published artifact's reproducibility window.
if [ -n "$(git ls-files --others --exclude-standard)" ]; then
    echo "ERROR: working tree has untracked files. Refusing to publish." >&2
    echo "       Remove or commit them, then retry." >&2
    git ls-files --others --exclude-standard >&2
    exit 2
fi

for asset in "${ASSETS[@]}"; do
    if [ ! -f "$asset" ]; then
        echo "ERROR: expected release asset missing: $asset" >&2
        exit 2
    fi
done

# Run the asset/scope linter on the notes file. It fails when a release
# note names a CorridorKey_*.<ext> file that is not in the asset list, or
# when the notes make a product claim for a backend that has no matching
# asset. This is the same class of check that was introduced for CI after
# the v0.7.4 incident; wiring it into the publish path closes the gap
# where a hand-authored notes file could still ship promises that don't
# match the uploaded binaries.
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LINTER="${REPO_ROOT}/scripts/lint_release_notes.py"
if [ -x "$LINTER" ] || [ -f "$LINTER" ]; then
    if ! command -v python3 >/dev/null 2>&1; then
        echo "ERROR: python3 is required to run $LINTER before publishing." >&2
        exit 2
    fi
    _assets_manifest="$(mktemp -t ck_publish_assets.XXXXXX)"
    trap 'rm -f "${_assets_manifest}"' EXIT
    for asset in "${ASSETS[@]}"; do
        basename "$asset" >> "$_assets_manifest"
    done
    echo "[publish] Linting release notes against declared assets..."
    if ! python3 "$LINTER" "$NOTES_FILE" --assets-file "$_assets_manifest"; then
        echo "ERROR: release notes linter rejected '$NOTES_FILE'." >&2
        echo "       Fix the notes or the asset list, then retry." >&2
        exit 2
    fi
fi

if [ -n "$DISPLAY_LABEL" ]; then
    TAG_LABEL="$DISPLAY_LABEL"
    RELEASE_FLAG="--prerelease"
    RELEASE_KIND="prerelease"
else
    TAG_LABEL="$VERSION"
    RELEASE_FLAG="--latest"
    RELEASE_KIND="stable latest"
fi
TAG_NAME="v${TAG_LABEL}"

# Tag immutability (absolute): refuse to re-publish a tag that already
# exists on the remote. The SemVer comparator in version_check.cpp
# trusts the tag name as ground truth, so mutating a published tag's
# assets breaks every installed client that cached a stale URL from it.
#
# gh release view exits non-zero when the release does not exist; under
# `set -e` that would terminate the script before we could inspect the
# outcome, so run it in a subshell that neutralizes the trap.
set +e
existing="$(gh release view "$TAG_NAME" --repo "$REPO" --json tagName 2>/dev/null)"
view_exit=$?
set -e
if [ $view_exit -eq 0 ] && [ -n "$existing" ]; then
    echo "ERROR: GitHub release '$TAG_NAME' already exists in $REPO." >&2
    echo "       Per docs/RELEASE_GUIDELINES.md, a published tag is immutable." >&2
    echo "       Bump the counter and retry." >&2
    exit 2
fi

case "$PLATFORM" in
    mac) TITLE="CorridorKey Resolve OFX v${TAG_LABEL} (macOS) - Apple Silicon" ;;
    linux) TITLE="CorridorKey Resolve OFX v${TAG_LABEL} (Linux)" ;;
    win) TITLE="CorridorKey Resolve OFX v${TAG_LABEL} (Windows)" ;;
esac

echo "[publish] tag:         $TAG_NAME"
echo "[publish] repo:        $REPO"
echo "[publish] kind:        $RELEASE_KIND"
echo "[publish] title:       $TITLE"
echo "[publish] notes file:  $NOTES_FILE"
echo "[publish] assets:"
for asset in "${ASSETS[@]}"; do
    echo "  - $asset"
done

gh_args=(
    release create "$TAG_NAME"
    --repo "$REPO"
    --title "$TITLE"
    --notes-file "$NOTES_FILE"
    "$RELEASE_FLAG"
)
for asset in "${ASSETS[@]}"; do
    gh_args+=("$asset")
done

if [ "$DRY_RUN" = "1" ]; then
    echo "[publish] DRY RUN — would invoke:"
    printf '  gh'
    for a in "${gh_args[@]}"; do
        printf ' %q' "$a"
    done
    printf '\n'
    exit 0
fi

echo "[publish] Invoking gh release create..."
gh "${gh_args[@]}"
echo "[publish] Published GitHub release $TAG_NAME in $REPO."
