#!/usr/bin/env bash
# Build ChemFox.app and a drag-to-Applications .dmg on macOS.
#
#   micromamba activate chemfox           # the environment from environment.yml
#   packaging/macos/build-mac.sh [prefix]  # prefix defaults to $CONDA_PREFIX
#
# Env: CHEMFOX_REPO_URL (for Help → Report a Problem), CHEMFOX_SKIP_TESTS=1 to skip the tests,
#      CHEMFOX_TESTS_WARN_ONLY=1 to carry on to the .dmg when a test fails.
# Output: build-mac/bin/ChemFox.app and dist/ChemFox-<version>-macos-<arch>.dmg
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PREFIX="${1:-${CONDA_PREFIX:-}}"
if [[ -z "$PREFIX" ]]; then echo "Activate the conda environment or pass its path." >&2; exit 2; fi
VERSION="$(sed -nE 's/^project\(ChemFox VERSION ([0-9.]+).*/\1/p' "$ROOT/CMakeLists.txt")"
ARCH="$(uname -m)"
BUILD="$ROOT/build-mac"
APP="$BUILD/bin/ChemFox.app"

echo "== Building ChemFox $VERSION for $ARCH"
cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH="$PREFIX" -DRDKIT_ROOT="$PREFIX" \
      -DCHEMFOX_REPO_URL="${CHEMFOX_REPO_URL:-https://github.com/VoxelMC/ChemFox}" \
      -DCMAKE_OSX_ARCHITECTURES="$ARCH"
cmake --build "$BUILD"

if [[ "${CHEMFOX_SKIP_TESTS:-0}" != "1" ]]; then
  echo "== Tests"
  if ! "$BUILD/bin/chemfox_tests"; then
    if [[ "${CHEMFOX_TESTS_WARN_ONLY:-0}" == "1" ]]; then echo "::warning::Some tests failed on macOS"; else exit 1; fi
  fi
fi

echo "== Bundling Qt, RDKit and friends into the app"
MACDEPLOYQT="$(command -v macdeployqt6 || command -v macdeployqt || true)"
[[ -z "$MACDEPLOYQT" && -x "$PREFIX/lib/qt6/bin/macdeployqt" ]] && MACDEPLOYQT="$PREFIX/lib/qt6/bin/macdeployqt"
[[ -z "$MACDEPLOYQT" && -x "$PREFIX/bin/macdeployqt6" ]] && MACDEPLOYQT="$PREFIX/bin/macdeployqt6"
if [[ -z "$MACDEPLOYQT" ]]; then echo "macdeployqt not found in $PREFIX" >&2; exit 1; fi
"$MACDEPLOYQT" "$APP" -libpath="$PREFIX/lib" -always-overwrite -verbose=1

# Samples, licences and notices travel inside the bundle.
RES="$APP/Contents/Resources"
rm -rf "$RES/samples" && cp -R "$ROOT/samples" "$RES/samples"
cp "$ROOT/LICENSE" "$ROOT/NOTICE" "$ROOT/CHANGELOG.md" "$RES/"
if command -v pwsh >/dev/null; then
  pwsh -NoProfile -File "$ROOT/packaging/collect-licenses.ps1" -Prefix "$PREFIX" -Dest "$RES/licenses" -Shipped "$APP"
fi

# Anything still pointing into the build environment would break on another Mac.
if otool -L "$APP/Contents/MacOS/ChemFox" "$APP"/Contents/Frameworks/*.dylib 2>/dev/null | grep -F "$PREFIX" ; then
  echo "warning: the libraries above still reference the build environment" >&2
fi

echo "== Signing (ad hoc; needed for Apple Silicon to run it at all)"
codesign --force --deep --sign - "$APP"
codesign --verify --deep --strict "$APP"

echo "== Disk image"
mkdir -p "$ROOT/dist"
STAGE="$(mktemp -d)"
cp -R "$APP" "$STAGE/"
ln -s /Applications "$STAGE/Applications"
DMG="$ROOT/dist/ChemFox-$VERSION-macos-$ARCH.dmg"
rm -f "$DMG"
hdiutil create -volname "ChemFox $VERSION" -srcfolder "$STAGE" -ov -format UDZO "$DMG"
rm -rf "$STAGE"
echo "== Done: $DMG"
