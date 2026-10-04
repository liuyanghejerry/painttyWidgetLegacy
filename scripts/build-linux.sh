#!/usr/bin/env bash
# Build all Linux targets with the Qt version used by this project.
set -euo pipefail

PROJECT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
QTDIR=${QTDIR:-"$HOME/develop/Qt/6.6.3/gcc_64"}
JOBS=${JOBS:-$(nproc)}
BUILD_CONFIG=release
OTHER_CONFIG=debug

while [[ $# -gt 0 ]]; do
    case "$1" in
        --debug) BUILD_CONFIG=debug; OTHER_CONFIG=release; shift ;;
        --jobs|-j)
            if [[ $# -lt 2 || ! $2 =~ ^[1-9][0-9]*$ ]]; then
                echo "--jobs requires a positive integer" >&2
                exit 1
            fi
            JOBS=$2; shift 2 ;;
        --help|-h)
            echo "Usage: $0 [--debug] [--jobs N]"
            echo "Override Qt location with QTDIR (default: $QTDIR)."
            exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

if [[ ! -x "$QTDIR/bin/qmake" ]]; then
    echo "Qt qmake not found: $QTDIR/bin/qmake" >&2
    echo "See docs/Qt6-Setup-Guide.md for Linux installation instructions." >&2
    exit 1
fi
for tool in g++ make; do
    command -v "$tool" >/dev/null || {
        echo "Missing $tool; install build-essential." >&2
        exit 1
    }
done

export PATH="$QTDIR/bin:$PATH"
mkdir -p "$PROJECT_DIR/build"
cd "$PROJECT_DIR/build"
"$QTDIR/bin/qmake" -v
"$QTDIR/bin/qmake" -r "$PROJECT_DIR/painttyWidget.pro" -spec linux-g++ \
    "CONFIG-=$OTHER_CONFIG" \
    "CONFIG+=$BUILD_CONFIG"
make -j"$JOBS"

for target in mrpaint canvas-renderer renderer-widget; do
    test -x "$PROJECT_DIR/build/build/$target"
done
echo "Build complete. Run: $PROJECT_DIR/build/build/mrpaint"
