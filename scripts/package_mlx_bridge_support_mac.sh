#!/bin/bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

if [ -f "${REPO_ROOT}/.env" ]; then
    set -a
    # shellcheck source=/dev/null
    source "${REPO_ROOT}/.env"
    set +a
fi

DEFAULT_VERSION="$(grep 'CORRIDORKEY_VERSION_STRING' "${REPO_ROOT}/include/corridorkey/version.hpp" | sed 's/.*"\(.*\)".*/\1/')"
VERSION="${CORRIDORKEY_BRIDGE_SUPPORT_VERSION:-${DEFAULT_VERSION}}"
SIGN_IDENTITY="${CORRIDORKEY_SIGN_IDENTITY:-}"
NOTARY_PROFILE="${CORRIDORKEY_NOTARY_PROFILE:-}"
ARCHIVE_FORMAT="${CORRIDORKEY_ARCHIVE_FORMAT:-dmg}"
PUBLIC_RELEASE="${CORRIDORKEY_PUBLIC_RELEASE:-0}"
IDENTITY_HELPER="${REPO_ROOT}/scripts/codesign-identity.sh"

PAYLOAD_ROOT="${REPO_ROOT}/build/mlx_bridge_support_payload"
WORK_DIR="${REPO_ROOT}/build/mlx_bridge_support_pkg"
DIST_DIR="${REPO_ROOT}/dist/CorridorKey_MLX_2048_Bridge_Support_v${VERSION}"
APP_NAME="CorridorKey Bridge Support.app"
APP_BUNDLE="${DIST_DIR}/${APP_NAME}"
APP_CONTENTS="${APP_BUNDLE}/Contents"
APP_MACOS="${APP_CONTENTS}/MacOS"
APP_RESOURCES="${APP_CONTENTS}/Resources"
PAYLOAD_DIR="${APP_RESOURCES}/payload"
DMG_PATH="${REPO_ROOT}/dist/CorridorKey_MLX_2048_Bridge_Support_v${VERSION}.dmg"
ZIP_PATH="${REPO_ROOT}/dist/CorridorKey_MLX_2048_Bridge_Support_v${VERSION}.zip"
README_PATH="${DIST_DIR}/README.txt"
APP_LAUNCHER="${APP_MACOS}/CorridorKeyBridgeSupport"
RUN_COMMAND="${APP_RESOURCES}/run_bridge_support.command"
APP_ID="com.corridorkey.bridge-support"

GENERATOR_SCRIPT="${REPO_ROOT}/scripts/generate_mlx_2048_bridge.sh"
PREPARE_SCRIPT="${REPO_ROOT}/scripts/prepare_mlx_model_pack.py"
CHECKPOINT_PATH="${REPO_ROOT}/models/CorridorKey.pth"
WEIGHTS_PATH="${REPO_ROOT}/models/corridorkey_mlx.safetensors"
EMBEDDED_VENV="${REPO_ROOT}/.venv-macos-mlx"
PYTHON_RUNTIME_LIB="${CORRIDORKEY_EMBEDDED_PYTHON_LIB:-}"
PYTHON_RUNTIME_HOME="${CORRIDORKEY_EMBEDDED_PYTHON_HOME:-}"

require_file() {
    local path="$1"
    if [ ! -e "$path" ]; then
        echo "ERROR: Missing required file: $path" >&2
        exit 1
    fi
}

sign_bundle() {
    local path="$1"

    if [ -n "$SIGN_IDENTITY" ]; then
        codesign --force --deep --options runtime --timestamp --sign "$SIGN_IDENTITY" "$path"
    else
        codesign --force --deep --sign - "$path"
    fi
}

sign_code_file() {
    local path="$1"

    if [ -n "$SIGN_IDENTITY" ]; then
        codesign --force --options runtime --timestamp --sign "$SIGN_IDENTITY" "$path"
    else
        codesign --force --sign - "$path"
    fi
}

is_macho_file() {
    local path="$1"
    file -b "$path" | grep -Eq 'Mach-O|universal binary'
}

materialize_embedded_python() {
    local venv_root="$1"
    local bin_dir="${venv_root}/bin"
    local source_path="${bin_dir}/python3"
    local source_target
    local staging_python

    if [ ! -e "$source_path" ]; then
        echo "ERROR: Missing embedded Python at ${source_path}." >&2
        exit 1
    fi

    if [ -L "$source_path" ]; then
        source_target="$(readlink "$source_path")"
        case "$source_target" in
            /*) ;;
            *) source_target="${bin_dir}/${source_target}" ;;
        esac
    else
        source_target="$source_path"
    fi

    if [ ! -f "$source_target" ]; then
        echo "ERROR: Could not resolve embedded Python target ${source_target}." >&2
        exit 1
    fi

    staging_python="$(mktemp "${WORK_DIR}/python3.XXXXXX")"
    cp "$source_target" "$staging_python"
    chmod +x "$staging_python"

    rm -f "${bin_dir}/python" "${bin_dir}/python3" "${bin_dir}/python3.13"
    cp "$staging_python" "${bin_dir}/python3"
    cp "$staging_python" "${bin_dir}/python"
    cp "$staging_python" "${bin_dir}/python3.13"
    chmod +x "${bin_dir}/python" "${bin_dir}/python3" "${bin_dir}/python3.13"

    rm -f "$staging_python"
}

resolve_embedded_python_runtime() {
    local python_bin="$1"
    local runtime_path
    local venv_root
    local pyvenv_cfg
    local python_home_bin
    local python_home_root

    if [ -n "$PYTHON_RUNTIME_LIB" ] && [ -f "$PYTHON_RUNTIME_LIB" ]; then
        printf '%s\n' "$PYTHON_RUNTIME_LIB"
        return 0
    fi

    runtime_path="$(otool -L "$python_bin" | awk '
        NR > 1 && $1 ~ /^@rpath\/libpython[0-9.]+\.dylib$/ {
            sub("^@rpath/", "", $1)
            print $1
            exit
        }
    ')"

    if [ -n "$runtime_path" ]; then
        venv_root="$(cd "$(dirname "$python_bin")/.." && pwd)"
        pyvenv_cfg="${venv_root}/pyvenv.cfg"
        if [ -f "$pyvenv_cfg" ]; then
            python_home_bin="$(awk -F' = ' '$1=="home" { print $2; exit }' "$pyvenv_cfg")"
            if [ -n "$python_home_bin" ]; then
                python_home_root="$(cd "$(dirname "$python_home_bin")" && pwd)"
            fi
        fi

        if [ -n "${python_home_root:-}" ] && [ -f "${python_home_root}/lib/${runtime_path}" ]; then
            printf '%s\n' "${python_home_root}/lib/${runtime_path}"
            return 0
        fi
    fi

    return 1
}

resolve_embedded_python_home() {
    local python_bin="$1"
    local venv_root
    local pyvenv_cfg
    local python_home_bin
    local python_home_root

    if [ -n "$PYTHON_RUNTIME_HOME" ] && [ -d "$PYTHON_RUNTIME_HOME" ]; then
        printf '%s\n' "$PYTHON_RUNTIME_HOME"
        return 0
    fi

    venv_root="$(cd "$(dirname "$python_bin")/.." && pwd)"
    pyvenv_cfg="${venv_root}/pyvenv.cfg"
    if [ ! -f "$pyvenv_cfg" ]; then
        return 1
    fi

    python_home_bin="$(awk -F' = ' '$1=="home" { print $2; exit }' "$pyvenv_cfg")"
    if [ -z "$python_home_bin" ]; then
        return 1
    fi

    python_home_root="$(cd "$(dirname "$python_home_bin")" && pwd)"
    if [ ! -d "${python_home_root}/lib" ]; then
        return 1
    fi

    printf '%s\n' "$python_home_root"
}

copy_portable_python_home() {
    local source_root="$1"
    local destination_root="$2"
    local stdlib_dir
    local stdlib_name
    local stdlib_zip

    stdlib_dir="$(find "${source_root}/lib" -maxdepth 1 -type d -name 'python3.*' | head -n 1)"
    if [ -z "$stdlib_dir" ]; then
        echo "ERROR: Could not locate the embedded Python standard library under ${source_root}/lib." >&2
        exit 1
    fi

    stdlib_name="$(basename "$stdlib_dir")"
    stdlib_zip="$(find "${source_root}/lib" -maxdepth 1 -type f -name 'python*.zip' | head -n 1)"

    mkdir -p "${destination_root}/bin" "${destination_root}/lib"
    ditto "$stdlib_dir" "${destination_root}/lib/${stdlib_name}"
    if [ -n "$stdlib_zip" ]; then
        cp "$stdlib_zip" "${destination_root}/lib/"
    fi
}

sign_nested_code() {
    local root="$1"
    local path

    while IFS= read -r path; do
        [ -f "$path" ] || continue
        if is_macho_file "$path"; then
            sign_code_file "$path"
        fi
    done < <(find "$root" -type f | awk '{ print length($0) " " $0 }' | sort -rn | cut -d' ' -f2-)
}

create_dmg_archive() {
    rm -f "$DMG_PATH"
    hdiutil create -volname "CorridorKey Bridge Support" -srcfolder "$DIST_DIR" \
        -ov -format UDZO "$DMG_PATH"
    if [ -n "$SIGN_IDENTITY" ]; then
        codesign --force --sign "$SIGN_IDENTITY" "$DMG_PATH"
    fi
}

create_zip_archive() {
    rm -f "$ZIP_PATH"
    ditto -c -k --norsrc --keepParent "$DIST_DIR" "$ZIP_PATH"
}

notarize_archive() {
    local archive_path="$1"
    xcrun notarytool submit "$archive_path" --keychain-profile "$NOTARY_PROFILE" --wait
    if [[ "$archive_path" == *.dmg ]]; then
        xcrun stapler staple "$archive_path"
        xcrun stapler validate "$archive_path"
    fi
}

require_public_release_prereqs() {
    if [ "$PUBLIC_RELEASE" != "1" ]; then
        return 0
    fi

    if [ "$ARCHIVE_FORMAT" != "dmg" ]; then
        echo "ERROR: Public support releases must use CORRIDORKEY_ARCHIVE_FORMAT=dmg." >&2
        exit 1
    fi
    if [ -z "$NOTARY_PROFILE" ]; then
        echo "ERROR: Public support releases require CORRIDORKEY_NOTARY_PROFILE." >&2
        exit 1
    fi
}

if [ -z "$SIGN_IDENTITY" ] && [ -x "$IDENTITY_HELPER" ]; then
    if [ "$PUBLIC_RELEASE" = "1" ] || [ -n "$NOTARY_PROFILE" ]; then
        SIGN_IDENTITY="$(bash "$IDENTITY_HELPER" public)"
    else
        SIGN_IDENTITY="$(bash "$IDENTITY_HELPER" release)"
    fi
    if [ "$SIGN_IDENTITY" = "-" ]; then
        SIGN_IDENTITY=""
    fi
fi

require_public_release_prereqs

require_file "$GENERATOR_SCRIPT"
require_file "$PREPARE_SCRIPT"
require_file "$CHECKPOINT_PATH"
require_file "$WEIGHTS_PATH"
require_file "${EMBEDDED_VENV}/bin/python"

if [ -z "$PYTHON_RUNTIME_LIB" ]; then
    PYTHON_RUNTIME_LIB="$(resolve_embedded_python_runtime "${EMBEDDED_VENV}/bin/python")" || {
        echo "ERROR: Could not locate the embedded Python runtime dylib." >&2
        exit 1
    }
fi
require_file "$PYTHON_RUNTIME_LIB"

if [ -z "$PYTHON_RUNTIME_HOME" ]; then
    PYTHON_RUNTIME_HOME="$(resolve_embedded_python_home "${EMBEDDED_VENV}/bin/python")" || {
        echo "ERROR: Could not locate the embedded Python home." >&2
        exit 1
    }
fi
require_file "${PYTHON_RUNTIME_HOME}/lib"

echo "[1/6] Cleaning packaging directories..."
rm -rf "$PAYLOAD_ROOT" "$WORK_DIR" "$DIST_DIR"
rm -f "$DMG_PATH" "$ZIP_PATH"
mkdir -p "$WORK_DIR" "$PAYLOAD_ROOT/scripts" "$PAYLOAD_ROOT/models" "$DIST_DIR" "$APP_MACOS" "$APP_RESOURCES"

echo "[2/6] Copying payload..."
cp "$GENERATOR_SCRIPT" "$PAYLOAD_ROOT/scripts/"
cp "$PREPARE_SCRIPT" "$PAYLOAD_ROOT/scripts/"
cp "$CHECKPOINT_PATH" "$PAYLOAD_ROOT/models/"
cp "$WEIGHTS_PATH" "$PAYLOAD_ROOT/models/"
ditto "$EMBEDDED_VENV" "${PAYLOAD_ROOT}/.venv-macos-mlx"
materialize_embedded_python "${PAYLOAD_ROOT}/.venv-macos-mlx"
copy_portable_python_home "$PYTHON_RUNTIME_HOME" "${PAYLOAD_ROOT}/.venv-macos-mlx/python-home"
cp "${PAYLOAD_ROOT}/.venv-macos-mlx/bin/python3" "${PAYLOAD_ROOT}/.venv-macos-mlx/python-home/bin/python3"
cp "${PAYLOAD_ROOT}/.venv-macos-mlx/bin/python" "${PAYLOAD_ROOT}/.venv-macos-mlx/python-home/bin/python"
cp "${PAYLOAD_ROOT}/.venv-macos-mlx/bin/python3.13" "${PAYLOAD_ROOT}/.venv-macos-mlx/python-home/bin/python3.13"
mkdir -p "${PAYLOAD_ROOT}/.venv-macos-mlx/lib"
cp "$PYTHON_RUNTIME_LIB" "${PAYLOAD_ROOT}/.venv-macos-mlx/lib/"
ditto "$PAYLOAD_ROOT" "$PAYLOAD_DIR"

echo "[3/6] Creating support app..."
cat <<'LAUNCHER_EOF' > "$APP_LAUNCHER"
#!/bin/bash
set -euo pipefail

APP_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
COMMAND_PATH="${APP_ROOT}/Resources/run_bridge_support.command"

open -a Terminal "$COMMAND_PATH"
LAUNCHER_EOF
chmod +x "$APP_LAUNCHER"

cat <<'COMMAND_EOF' > "$RUN_COMMAND"
#!/bin/bash
set -euo pipefail

RESOURCE_DIR="$(cd "$(dirname "$0")" && pwd)"
PAYLOAD_ROOT="${RESOURCE_DIR}/payload"
OUTPUT_ROOT="${HOME}/Library/Application Support/CorridorKey Bridge Support"

mkdir -p "$OUTPUT_ROOT"

export CORRIDORKEY_SUPPORT_ROOT="$PAYLOAD_ROOT"
export CORRIDORKEY_SUPPORT_OUTPUT_ROOT="$OUTPUT_ROOT"
export CORRIDORKEY_MLX_PYTHON="${PAYLOAD_ROOT}/.venv-macos-mlx/bin/python"
export CORRIDORKEY_EMBEDDED_PYTHON_HOME="${PAYLOAD_ROOT}/.venv-macos-mlx/python-home"

exec "${PAYLOAD_ROOT}/scripts/generate_mlx_2048_bridge.sh" "$@"
COMMAND_EOF
chmod +x "$RUN_COMMAND"

cat <<PLIST_EOF > "${APP_CONTENTS}/Info.plist"
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key>
    <string>en</string>
    <key>CFBundleDisplayName</key>
    <string>CorridorKey Bridge Support</string>
    <key>CFBundleExecutable</key>
    <string>CorridorKeyBridgeSupport</string>
    <key>CFBundleIdentifier</key>
    <string>${APP_ID}</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundleName</key>
    <string>CorridorKey Bridge Support</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>${VERSION}</string>
    <key>CFBundleVersion</key>
    <string>${VERSION}</string>
    <key>LSMinimumSystemVersion</key>
    <string>14.0</string>
    <key>NSHighResolutionCapable</key>
    <true/>
</dict>
</plist>
PLIST_EOF

cat <<README_EOF > "$README_PATH"
CorridorKey MLX 2048 Bridge Support
===================================

1. Open "CorridorKey Bridge Support.app".
2. If your Mac has enough memory, let the process finish.
3. At the end, Finder will reveal the files you need to send back.

The generated files are stored under:
~/Library/Application Support/CorridorKey Bridge Support
README_EOF

find "$DIST_DIR" \( -name '.DS_Store' -o -name '._*' \) -delete

echo "[4/6] Signing app bundle..."
sign_nested_code "$APP_BUNDLE"
sign_bundle "$APP_BUNDLE"
codesign --verify --deep --verbose=2 "$APP_BUNDLE"

echo "[5/6] Creating archive..."
case "$ARCHIVE_FORMAT" in
    dmg)
        create_dmg_archive
        ARCHIVE_PATH="$DMG_PATH"
        ;;
    zip)
        create_zip_archive
        ARCHIVE_PATH="$ZIP_PATH"
        ;;
    *)
        echo "ERROR: Unsupported CORRIDORKEY_ARCHIVE_FORMAT '$ARCHIVE_FORMAT'. Use zip or dmg." >&2
        exit 1
        ;;
esac

if [ -n "$NOTARY_PROFILE" ]; then
    echo "[6/6] Submitting archive for notarization..."
    notarize_archive "$ARCHIVE_PATH"
else
    echo "[6/6] Skipping notarization because CORRIDORKEY_NOTARY_PROFILE is not set."
fi

echo "=================================================="
echo "SUCCESS: Support package created at $ARCHIVE_PATH"
echo "=================================================="
