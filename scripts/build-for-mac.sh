#!/bin/bash

set -e

# 如果QTDIR变量未设置，则报错终止
if [ -z "$QTDIR" ]; then
    echo "QTDIR变量未设置，请设置QTDIR变量后运行此脚本"
    exit 1
fi

export PATH="$QTDIR/bin:$PATH"

# 获取脚本所在目录的绝对路径
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
BUILD_DIR=$(cd "$SCRIPT_DIR/../build" && pwd)
PROJECT_DIR=$(cd "$SCRIPT_DIR/.." && pwd)
UPDATER_DIR=$(cd "$SCRIPT_DIR/../../painttyUpdater/bin" && pwd)

echo "PROJECT_DIR: $PROJECT_DIR"
echo "BUILD_DIR: $BUILD_DIR"

QMAKE_PATH="$QTDIR/clang_64/bin/qmake"
DEPLOY_PATH="$QTDIR/clang_64/bin/macdeployqt"

# 先执行qmake
$QMAKE_PATH "$PROJECT_DIR/painttyWidget.pro" -spec macx-clang CONFIG+=qtquickcompiler && /usr/bin/make qmake_all

# 再执行make
cd "$BUILD_DIR/main-Release" && /usr/bin/make -j10
cd "$BUILD_DIR/main_intel-Release" && /usr/bin/make -j10

# 再执行macdeployqt
$DEPLOY_PATH "$BUILD_DIR/main-Release/build/MrPaint.app"
cp "$UPDATER_DIR/darwin_arm64/updater" "$BUILD_DIR/main-Release/build/MrPaint.app/Contents/MacOS/"
$DEPLOY_PATH "$BUILD_DIR/main_intel-Release/build/MrPaint.app"
cp "$UPDATER_DIR/darwin_amd64/updater" "$BUILD_DIR/main_intel-Release/build/MrPaint.app/Contents/MacOS/"

# 将.app文件打包为zip文件
ditto -c -k --sequesterRsrc --keepParent "$BUILD_DIR/main-Release/build/MrPaint.app" "$BUILD_DIR/main-Release/build/MrPaint.zip"
ditto -c -k --sequesterRsrc --keepParent "$BUILD_DIR/main_intel-Release/build/MrPaint.app" "$BUILD_DIR/main_intel-Release/build/MrPaint.zip"

echo "Build for mac completed successfully!"
