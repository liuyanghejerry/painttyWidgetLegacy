#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
QTDIR=${QTDIR:-"$HOME/develop/Qt/6.6.3/gcc_64"}
JOBS=${JOBS:-8}
mkdir -p "$PROJECT_DIR/build/local-tests"
cd "$PROJECT_DIR/build/local-tests"
"$QTDIR/bin/qmake" "$PROJECT_DIR/src/painttyDesktop/local-tests.pro" CONFIG-=debug CONFIG+=release
make -j"$JOBS"
QT_QPA_PLATFORM=${QT_QPA_PLATFORM:-offscreen} ./bin/local-tests "$@"
