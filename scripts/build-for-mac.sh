#!/usr/bin/env bash
# Build a universal macOS application and run the interaction regression suite.
set -euo pipefail

if [[ ${1:-} == --help ]]; then
    echo 'Usage: QTDIR=/path/to/Qt/6.6.3/macos ./scripts/build-for-mac.sh'
    echo 'Optional overrides: PROJECT_DIR, BUILD_DIR, JOBS.'
    exit 0
fi
[[ $(uname -s) == Darwin ]] || { echo 'This script requires macOS.' >&2; exit 1; }
PROJECT_DIR=${PROJECT_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}
BUILD_DIR=${BUILD_DIR:-"$PROJECT_DIR/build/macos"}
: "${QTDIR:?Set QTDIR to the Qt 6.6.3 macos directory.}"
[[ $("$QTDIR/bin/qmake" -query QT_VERSION) == 6.6.3 ]] || { echo 'Qt 6.6.3 is required.' >&2; exit 1; }
JOBS=${JOBS:-$(sysctl -n hw.logicalcpu)}
mkdir -p "$BUILD_DIR/src/painttyDesktop" "$BUILD_DIR/tests"
BUILD_DIR=$(cd "$BUILD_DIR" && pwd)
export PATH="$QTDIR/bin:$PATH"

# Match commonconfigure.pri's relative output directory without building legacy renderers.
cd "$BUILD_DIR/src/painttyDesktop"
"$QTDIR/bin/qmake" "$PROJECT_DIR/src/painttyDesktop/painttyDesktop.pro" \
    CONFIG-=debug CONFIG+=release 'QMAKE_APPLE_DEVICE_ARCHS=x86_64 arm64'
make -j"$JOBS"
lipo "$BUILD_DIR/build/MrPaint.app/Contents/MacOS/MrPaint" -verify_arch x86_64 arm64

cd "$BUILD_DIR/tests"
"$QTDIR/bin/qmake" "$PROJECT_DIR/src/painttyDesktop/local-tests.pro" \
    CONFIG-=debug CONFIG+=release CONFIG-=app_bundle
make -j"$JOBS"
QT_QPA_PLATFORM=offscreen ./bin/local-tests \
    -o "$BUILD_DIR/test-results.txt",txt -o "$BUILD_DIR/test-results.xml",junitxml
cat "$BUILD_DIR/test-results.txt"
echo "Build complete: $BUILD_DIR/build/MrPaint.app"
