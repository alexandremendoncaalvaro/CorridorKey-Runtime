#!/usr/bin/env bash
# Linux OFX packaging - emits .tar.gz, .deb, and .rpm from a single staged
# CorridorKey.ofx.bundle. All three artifacts embed the same validated
# bundle; they differ only in the distribution wrapper and the post-install
# symlink lifecycle. Usage:
#   scripts/package_ofx_installer_linux.sh --version 0.7.5 [--build-dir build/release-linux-portable]

set -euo pipefail

VERSION=""
BUILD_DIR=""
SKIP_DEB=0
SKIP_RPM=0
SKIP_TAR=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --version) VERSION="$2"; shift 2 ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --skip-deb) SKIP_DEB=1; shift ;;
        --skip-rpm) SKIP_RPM=1; shift ;;
        --skip-tar) SKIP_TAR=1; shift ;;
        --help|-h)
            cat <<USAGE
Usage: package_ofx_installer_linux.sh --version X.Y.Z [options]

Options:
  --version X.Y.Z     Release version (required).
  --build-dir PATH    CMake build directory (default: build/release-linux-portable).
  --skip-deb          Do not emit the Debian package.
  --skip-rpm          Do not emit the RPM package.
  --skip-tar          Do not emit the tarball.
USAGE
            exit 0
            ;;
        *) echo "Unknown argument: $1" >&2; exit 1 ;;
    esac
done

if [[ -z "$VERSION" ]]; then
    echo "--version is required" >&2
    exit 1
fi

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

if [[ -z "$BUILD_DIR" ]]; then
    BUILD_DIR="$REPO_ROOT/build/release-linux-portable"
fi

BUNDLE_SRC="$BUILD_DIR/CorridorKey.ofx.bundle"
if [[ ! -d "$BUNDLE_SRC" ]]; then
    echo "Staged bundle not found at $BUNDLE_SRC" >&2
    echo "Run: scripts/linux.sh --task build first" >&2
    exit 1
fi

CONTENTS_DIR="$BUNDLE_SRC/Contents/Linux-x86_64"
if [[ ! -f "$CONTENTS_DIR/CorridorKey.ofx" ]] || [[ ! -f "$CONTENTS_DIR/corridorkey" ]]; then
    echo "Staged bundle is incomplete - missing CorridorKey.ofx or corridorkey under $CONTENTS_DIR" >&2
    exit 1
fi

RELEASE_NAME="CorridorKey_Resolve_v${VERSION}_Linux_RTX"
DIST_DIR="$REPO_ROOT/dist"
STAGE_DIR="$DIST_DIR/$RELEASE_NAME"

rm -rf "$STAGE_DIR"
mkdir -p "$STAGE_DIR"

# Copy the validated bundle payload unchanged.
cp -R "$BUNDLE_SRC" "$STAGE_DIR/"

# Tarball install/uninstall helpers. The Debian postinst and the RPM %post
# perform the equivalent work through their package manager lifecycles.
cat > "$STAGE_DIR/install.sh" <<'INSTALL'
#!/usr/bin/env bash
set -euo pipefail
if [[ $EUID -ne 0 ]]; then
    echo "install.sh must run as root (try: sudo ./install.sh)" >&2
    exit 1
fi
here="$(cd "$(dirname "$0")" && pwd)"
target="/usr/OFX/Plugins/CorridorKey.ofx.bundle"
rm -rf "$target"
mkdir -p "$(dirname "$target")"
cp -R "$here/CorridorKey.ofx.bundle" "$target"
cli="$target/Contents/Linux-x86_64/corridorkey"
chmod +x "$cli"
ln -snf "$cli" /usr/local/bin/corridorkey
echo "CorridorKey installed. The 'corridorkey' CLI is on PATH via /usr/local/bin."
echo "Launch DaVinci Resolve Studio 20 and load the plugin from the OpenFX Library."
INSTALL
chmod +x "$STAGE_DIR/install.sh"

cat > "$STAGE_DIR/uninstall.sh" <<'UNINSTALL'
#!/usr/bin/env bash
set -euo pipefail
if [[ $EUID -ne 0 ]]; then
    echo "uninstall.sh must run as root (try: sudo ./uninstall.sh)" >&2
    exit 1
fi
rm -f /usr/local/bin/corridorkey
rm -rf /usr/OFX/Plugins/CorridorKey.ofx.bundle
echo "CorridorKey removed."
UNINSTALL
chmod +x "$STAGE_DIR/uninstall.sh"

cat > "$STAGE_DIR/README.txt" <<README
CorridorKey Resolve OFX v${VERSION} - Linux RTX (Experimental)

This archive installs the CorridorKey OpenFX plugin and its companion CLI
into the system OpenFX directory under /usr/OFX/Plugins/ and registers
corridorkey on PATH via /usr/local/bin. DaVinci Resolve Studio 20 is
required (the free Resolve edition does not load OpenFX plugins on Linux).

Install:
  sudo ./install.sh

Uninstall:
  sudo ./uninstall.sh

A proprietary NVIDIA driver 555 or newer is required. The CUDA Toolkit is
NOT required - the CUDA user-mode libraries ship inside the bundle.
README

if [[ $SKIP_TAR -eq 0 ]]; then
    echo "[linux-package] emitting tarball"
    tar -C "$DIST_DIR" -czf "$DIST_DIR/${RELEASE_NAME}.tar.gz" "$RELEASE_NAME"
    echo "  -> $DIST_DIR/${RELEASE_NAME}.tar.gz"
fi

# ---------------------------------------------------------------------------
# Debian package (.deb) - data.tar.gz layout mirrors the on-disk install
# from install.sh: the bundle lives at /usr/OFX/Plugins/CorridorKey.ofx.bundle/
# and the symlink /usr/local/bin/corridorkey is created in the postinst.
# ---------------------------------------------------------------------------
if [[ $SKIP_DEB -eq 0 ]]; then
    if ! command -v dpkg-deb >/dev/null 2>&1; then
        echo "[linux-package] dpkg-deb not found - skipping .deb" >&2
    else
        echo "[linux-package] emitting .deb"
        DEB_ROOT="$DIST_DIR/deb-build"
        rm -rf "$DEB_ROOT"
        mkdir -p "$DEB_ROOT/DEBIAN" "$DEB_ROOT/usr/OFX/Plugins"
        cp -R "$BUNDLE_SRC" "$DEB_ROOT/usr/OFX/Plugins/"

        # Install package metadata from the checked-in source of truth.
        cp "$REPO_ROOT/packaging/linux/deb/control.in" "$DEB_ROOT/DEBIAN/control"
        sed -i "s/@VERSION@/${VERSION}/g" "$DEB_ROOT/DEBIAN/control"
        cp "$REPO_ROOT/packaging/linux/deb/postinst" "$DEB_ROOT/DEBIAN/postinst"
        cp "$REPO_ROOT/packaging/linux/deb/prerm" "$DEB_ROOT/DEBIAN/prerm"
        chmod 0755 "$DEB_ROOT/DEBIAN/postinst" "$DEB_ROOT/DEBIAN/prerm"

        DEB_PATH="$DIST_DIR/${RELEASE_NAME}.deb"
        dpkg-deb --root-owner-group --build "$DEB_ROOT" "$DEB_PATH"
        rm -rf "$DEB_ROOT"
        echo "  -> $DEB_PATH"
    fi
fi

# ---------------------------------------------------------------------------
# RPM package (.rpm) - built with rpmbuild -bb from the spec file in
# packaging/linux/rpm/. The %post/%preun scriptlets mirror the .deb lifecycle.
# ---------------------------------------------------------------------------
if [[ $SKIP_RPM -eq 0 ]]; then
    if ! command -v rpmbuild >/dev/null 2>&1; then
        echo "[linux-package] rpmbuild not found - skipping .rpm" >&2
    else
        echo "[linux-package] emitting .rpm"
        RPM_ROOT="$DIST_DIR/rpm-build"
        rm -rf "$RPM_ROOT"
        mkdir -p "$RPM_ROOT/BUILD" "$RPM_ROOT/RPMS" "$RPM_ROOT/SOURCES" "$RPM_ROOT/SPECS" "$RPM_ROOT/SRPMS" "$RPM_ROOT/BUILDROOT"

        # Stage the bundle under BUILD/ so the spec can copy from a relative path.
        mkdir -p "$RPM_ROOT/BUILD/payload/usr/OFX/Plugins"
        cp -R "$BUNDLE_SRC" "$RPM_ROOT/BUILD/payload/usr/OFX/Plugins/"

        SPEC_IN="$REPO_ROOT/packaging/linux/rpm/corridorkey-resolve-ofx.spec.in"
        SPEC_OUT="$RPM_ROOT/SPECS/corridorkey-resolve-ofx.spec"
        sed "s/@VERSION@/${VERSION}/g" "$SPEC_IN" > "$SPEC_OUT"

        rpmbuild --define "_topdir $RPM_ROOT" --define "_payload_dir $RPM_ROOT/BUILD/payload" -bb "$SPEC_OUT"

        RPM_BUILT="$(find "$RPM_ROOT/RPMS" -name '*.rpm' -print -quit)"
        if [[ -n "$RPM_BUILT" ]]; then
            cp "$RPM_BUILT" "$DIST_DIR/${RELEASE_NAME}.rpm"
            echo "  -> $DIST_DIR/${RELEASE_NAME}.rpm"
        else
            echo "[linux-package] rpmbuild completed but no .rpm was produced" >&2
        fi
        rm -rf "$RPM_ROOT"
    fi
fi

echo "[linux-package] done"
