#!/bin/bash
# smoke_extract_and_run.sh — extract a packaged release artifact in a clean
# directory and run the CLI against it.
#
# Why
# ---
# The pre-release v0.7.4 shipped a release-notes file that named binaries that
# were never uploaded as assets, and we discovered the gap days later because
# CI never touched the packaged artifact — only the build tree. This script is
# the Tier 2 / Tier 3 gate that proves the artifact a user would actually
# download is launchable.
#
# Behavior
# --------
# 1. Resolve the artifact (zip or dmg) given on the command line.
# 2. Extract into a freshly-created temp directory outside the source tree.
# 3. Locate the `corridorkey` binary inside the extracted layout.
# 4. Run `corridorkey --version --json` and assert:
#    - exit code 0
#    - stdout parses as JSON
#    - the `version` field matches --expected-version (when provided)
# 5. Clean up the temp directory unless --keep is given.
#
# Usage
# -----
#   scripts/smoke_extract_and_run.sh dist/CorridorKey_Resolve_v0.7.5_macOS.zip
#   scripts/smoke_extract_and_run.sh dist/CorridorKey.dmg --expected-version 0.7.5
#   scripts/smoke_extract_and_run.sh dist/CorridorKey.zip --keep
#
# Exit codes
#   0 = smoke passed
#   1 = smoke failed (artifact missing, extraction failed, run failed, version mismatch)
#   2 = bad invocation
set -euo pipefail

ARTIFACT=""
EXPECTED_VERSION=""
KEEP_TEMP=0

while [ $# -gt 0 ]; do
    case "$1" in
        --expected-version)
            EXPECTED_VERSION="${2:-}"
            shift 2
            ;;
        --expected-version=*)
            EXPECTED_VERSION="${1#*=}"
            shift
            ;;
        --keep)
            KEEP_TEMP=1
            shift
            ;;
        -h|--help)
            sed -n '1,/^set -euo/p' "$0" | sed '$d'
            exit 0
            ;;
        --*)
            echo "error: unknown option: $1" >&2
            exit 2
            ;;
        *)
            if [ -z "$ARTIFACT" ]; then
                ARTIFACT="$1"
            else
                echo "error: unexpected positional argument: $1" >&2
                exit 2
            fi
            shift
            ;;
    esac
done

if [ -z "$ARTIFACT" ]; then
    echo "error: artifact path required" >&2
    echo "usage: $0 <artifact.zip|artifact.dmg> [--expected-version X.Y.Z] [--keep]" >&2
    exit 2
fi

if [ ! -f "$ARTIFACT" ]; then
    echo "error: artifact not found: $ARTIFACT" >&2
    exit 1
fi

ARTIFACT_ABS="$(cd "$(dirname "$ARTIFACT")" && pwd)/$(basename "$ARTIFACT")"
TEMP_ROOT="$(mktemp -d -t ck-smoke.XXXXXXXX)"

cleanup() {
    if [ "$KEEP_TEMP" = "1" ]; then
        echo "[smoke] keeping temp dir: $TEMP_ROOT"
        return
    fi
    if [ -d "$TEMP_ROOT" ]; then
        # On macOS, detach any DMGs we mounted.
        if command -v hdiutil >/dev/null 2>&1; then
            for mount in "$TEMP_ROOT"/mount-*; do
                [ -d "$mount" ] || continue
                hdiutil detach "$mount" -quiet 2>/dev/null || true
            done
        fi
        rm -rf "$TEMP_ROOT"
    fi
}
trap cleanup EXIT

echo "[smoke] artifact: $ARTIFACT_ABS"
echo "[smoke] temp:     $TEMP_ROOT"

EXTRACT_DIR="$TEMP_ROOT/extract"
mkdir -p "$EXTRACT_DIR"

case "$ARTIFACT_ABS" in
    *.zip)
        echo "[smoke] extracting zip"
        if command -v unzip >/dev/null 2>&1; then
            unzip -q "$ARTIFACT_ABS" -d "$EXTRACT_DIR"
        else
            # ditto preserves macOS resource forks/notarization xattrs that
            # unzip drops; prefer it when available.
            ditto -x -k "$ARTIFACT_ABS" "$EXTRACT_DIR"
        fi
        ;;
    *.dmg)
        if ! command -v hdiutil >/dev/null 2>&1; then
            echo "error: .dmg artifacts require macOS (hdiutil not found)" >&2
            exit 1
        fi
        MOUNT_DIR="$TEMP_ROOT/mount-1"
        mkdir -p "$MOUNT_DIR"
        echo "[smoke] mounting dmg at $MOUNT_DIR"
        hdiutil attach "$ARTIFACT_ABS" -mountpoint "$MOUNT_DIR" -nobrowse -quiet
        # Copy out so we exercise the same code path as a user dragging the
        # bundle to /Applications. Mounted DMG is read-only and may carry
        # quarantine bits we want stripped to mirror an installed copy.
        cp -R "$MOUNT_DIR"/* "$EXTRACT_DIR/"
        ;;
    *.tar.gz|*.tgz)
        echo "[smoke] extracting tarball"
        tar -xzf "$ARTIFACT_ABS" -C "$EXTRACT_DIR"
        ;;
    *)
        echo "error: unsupported artifact extension: $ARTIFACT_ABS" >&2
        echo "       supported: .zip, .dmg, .tar.gz, .tgz" >&2
        exit 1
        ;;
esac

# Locate the corridorkey binary. The packaged layout varies between the CLI
# bundle and the OFX bundle; search for the executable rather than hard-coding
# a path so the smoke survives layout tweaks.
CLI_PATH="$(find "$EXTRACT_DIR" -type f \( -name corridorkey -o -name corridorkey.exe \) -perm -u+x -print -quit 2>/dev/null || true)"
if [ -z "$CLI_PATH" ]; then
    # macOS .app bundles may have non-executable bits when extracted via cp -R
    # from a mount; fall back to a name-only search and we'll chmod on demand.
    CLI_PATH="$(find "$EXTRACT_DIR" -type f \( -name corridorkey -o -name corridorkey.exe \) -print -quit 2>/dev/null || true)"
    if [ -n "$CLI_PATH" ]; then
        chmod +x "$CLI_PATH"
    fi
fi

if [ -z "$CLI_PATH" ]; then
    echo "error: no corridorkey binary found inside $EXTRACT_DIR" >&2
    echo "[smoke] artifact contents:" >&2
    find "$EXTRACT_DIR" -maxdepth 4 -print >&2
    exit 1
fi

echo "[smoke] cli path: $CLI_PATH"
echo "[smoke] running: $CLI_PATH --version --json"

# Run from a directory that does NOT contain the source tree, to catch any
# accidental dependency on $REPO_ROOT/models or $REPO_ROOT/help that would
# silently work on a developer machine but fail for an end user.
cd "$TEMP_ROOT"

if ! VERSION_JSON="$("$CLI_PATH" --version --json 2>&1)"; then
    echo "error: corridorkey --version exited non-zero" >&2
    echo "[smoke] output:" >&2
    echo "$VERSION_JSON" >&2
    exit 1
fi

echo "[smoke] output: $VERSION_JSON"

# Validate JSON structure and extract version. Use python3 because it's
# available on every macOS / Linux runner without extra setup; jq is not
# guaranteed on macos-14 GitHub runners.
if ! REPORTED_VERSION="$(printf '%s' "$VERSION_JSON" | python3 -c '
import json, sys
data = json.loads(sys.stdin.read())
v = data.get("version")
if not v:
    sys.exit("missing .version field in --version output")
print(v)
')"; then
    echo "error: --version output is not valid JSON or lacks .version field" >&2
    exit 1
fi

echo "[smoke] reported version: $REPORTED_VERSION"

if [ -n "$EXPECTED_VERSION" ] && [ "$REPORTED_VERSION" != "$EXPECTED_VERSION" ]; then
    echo "error: version mismatch — artifact reports '$REPORTED_VERSION', expected '$EXPECTED_VERSION'" >&2
    echo "       this means the artifact was built from the wrong source ref or with the wrong" >&2
    echo "       CORRIDORKEY_DISPLAY_VERSION_LABEL." >&2
    exit 1
fi

echo "[smoke] PASS"
