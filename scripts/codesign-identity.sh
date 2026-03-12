#!/usr/bin/env bash
set -euo pipefail

MODE="${1:-preview}"

if [[ -n "${CORRIDORKEY_SIGN_IDENTITY:-}" ]]; then
    printf "%s\n" "$CORRIDORKEY_SIGN_IDENTITY"
    exit 0
fi

IDENTITIES="$(security find-identity -v -p codesigning 2>/dev/null || true)"

pick_identity() {
    local prefix="$1"
    printf "%s\n" "$IDENTITIES" | awk -F'"' -v prefix="$prefix" '
        $2 ~ ("^" prefix) {
            gsub(/^[[:space:]]+/, "", $1)
            split($1, parts, /[[:space:]]+/)
            print parts[2]
            exit
        }
    '
}

case "$MODE" in
    public)
        IDENTITY="$(pick_identity "Developer ID Application:")"
        ;;
    release)
        IDENTITY="$(pick_identity "Developer ID Application:")"
        if [[ -z "$IDENTITY" ]]; then
            IDENTITY="$(pick_identity "Apple Distribution:")"
        fi
        if [[ -z "$IDENTITY" ]]; then
            IDENTITY="$(pick_identity "Apple Development:")"
        fi
        ;;
    preview|dev|debug)
        IDENTITY="$(pick_identity "Apple Development:")"
        if [[ -z "$IDENTITY" ]]; then
            IDENTITY="$(pick_identity "Developer ID Application:")"
        fi
        if [[ -z "$IDENTITY" ]]; then
            IDENTITY="$(pick_identity "Apple Distribution:")"
        fi
        ;;
    *)
        echo "usage: scripts/codesign-identity.sh <preview|release|public|dev|debug>" >&2
        exit 1
        ;;
esac

if [[ -n "${IDENTITY:-}" ]]; then
    printf "%s\n" "$IDENTITY"
    exit 0
fi

printf "%s\n" "-"
