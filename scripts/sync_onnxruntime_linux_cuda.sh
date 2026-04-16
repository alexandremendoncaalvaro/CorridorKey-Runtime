#!/bin/bash
# Stage the curated Linux ONNX Runtime GPU (CUDA EP) drop used by the
# experimental CorridorKey Linux RTX track.
#
# Downloads the official Microsoft prebuilt from the ONNX Runtime GitHub
# release, verifies the pinned SHA256, and extracts it to
# vendor/onnxruntime-linux-cuda/ with the same include/ + lib/ layout the
# CMake discovery in CMakeLists.txt expects.
#
# Usage:
#   scripts/sync_onnxruntime_linux_cuda.sh [--version X.Y.Z]
#
# Env overrides:
#   CORRIDORKEY_LINUX_ORT_VERSION  -> pinned release tag (e.g. 1.23.0)
#   CORRIDORKEY_LINUX_ORT_SHA256   -> pinned archive hash

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEFAULT_VERSION="1.23.0"
DEFAULT_SHA256=""  # Pin before first release; empty means skip verification with a loud warning.

VERSION="${CORRIDORKEY_LINUX_ORT_VERSION:-${DEFAULT_VERSION}}"
SHA256="${CORRIDORKEY_LINUX_ORT_SHA256:-${DEFAULT_SHA256}}"

while [ $# -gt 0 ]; do
    case "$1" in
        --version)
            VERSION="$2"
            shift 2
            ;;
        --sha256)
            SHA256="$2"
            shift 2
            ;;
        -h|--help)
            cat <<USAGE
Usage: $0 [--version X.Y.Z] [--sha256 HEX]

Stages the curated Linux ONNX Runtime GPU bundle at
vendor/onnxruntime-linux-cuda/. Requires curl or wget.
USAGE
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            exit 2
            ;;
    esac
done

OUTPUT_DIR="${REPO_ROOT}/vendor/onnxruntime-linux-cuda"
ARCHIVE_NAME="onnxruntime-linux-x64-gpu-${VERSION}.tgz"
ARCHIVE_URL="https://github.com/microsoft/onnxruntime/releases/download/v${VERSION}/${ARCHIVE_NAME}"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "${TMP_DIR}"' EXIT

ARCHIVE_PATH="${TMP_DIR}/${ARCHIVE_NAME}"

echo "[sync-ort-linux] Downloading ${ARCHIVE_URL}"
if command -v curl >/dev/null 2>&1; then
    curl -fL --retry 3 --retry-delay 2 "${ARCHIVE_URL}" -o "${ARCHIVE_PATH}"
elif command -v wget >/dev/null 2>&1; then
    wget -q -O "${ARCHIVE_PATH}" "${ARCHIVE_URL}"
else
    echo "ERROR: neither curl nor wget is available" >&2
    exit 1
fi

if [ -n "${SHA256}" ]; then
    echo "[sync-ort-linux] Verifying SHA256"
    echo "${SHA256}  ${ARCHIVE_PATH}" | sha256sum -c -
else
    echo "[sync-ort-linux] WARNING: no SHA256 pinned; skipping integrity check" >&2
fi

echo "[sync-ort-linux] Extracting into ${OUTPUT_DIR}"
rm -rf "${OUTPUT_DIR}"
mkdir -p "${OUTPUT_DIR}"

# The upstream archive extracts into a versioned top-level directory like
# onnxruntime-linux-x64-gpu-1.23.0/. We strip that to land include/ and lib/
# directly under OUTPUT_DIR, matching the Windows curated layout.
tar -xzf "${ARCHIVE_PATH}" -C "${TMP_DIR}"
SRC_DIR="${TMP_DIR}/onnxruntime-linux-x64-gpu-${VERSION}"
if [ ! -d "${SRC_DIR}" ]; then
    SRC_DIR="$(find "${TMP_DIR}" -maxdepth 1 -mindepth 1 -type d -name 'onnxruntime-linux-x64-gpu-*' | head -n 1)"
fi
if [ -z "${SRC_DIR}" ] || [ ! -d "${SRC_DIR}" ]; then
    echo "ERROR: could not locate extracted ONNX Runtime directory" >&2
    exit 1
fi

cp -R "${SRC_DIR}/include" "${OUTPUT_DIR}/"
cp -R "${SRC_DIR}/lib" "${OUTPUT_DIR}/"
if [ -f "${SRC_DIR}/LICENSE" ]; then
    cp "${SRC_DIR}/LICENSE" "${OUTPUT_DIR}/LICENSE.onnxruntime.txt"
fi
if [ -f "${SRC_DIR}/VERSION_NUMBER" ]; then
    cp "${SRC_DIR}/VERSION_NUMBER" "${OUTPUT_DIR}/VERSION_NUMBER"
fi

echo "[sync-ort-linux] Curated Linux ONNX Runtime staged at ${OUTPUT_DIR}"
echo "[sync-ort-linux] Version: ${VERSION}"
