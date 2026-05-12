#!/bin/bash
#
# painttyWidget Qt 6 编译脚本 (macOS + Qt 6.6.3 aqt)
#

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

DEFAULT_QTDIR="/Users/liuyanghejerry/develop/qt/6.6.3/macos"

if [ -z "$QTDIR" ]; then
    echo -e "${YELLOW}QTDIR 未设置，使用默认路径: $DEFAULT_QTDIR${NC}"
    QTDIR="$DEFAULT_QTDIR"
fi

if [ ! -d "$QTDIR" ]; then
    echo -e "${RED}错误: Qt 目录不存在: $QTDIR${NC}"
    echo "请先安装 Qt 或设置正确的 QTDIR 环境变量"
    echo "参考: docs/Qt6-Setup-Guide.md"
    exit 1
fi

QMAKE_PATH="$QTDIR/bin/qmake"
if [ ! -f "$QMAKE_PATH" ]; then
    echo -e "${RED}错误: qmake 不存在: $QMAKE_PATH${NC}"
    exit 1
fi

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
PROJECT_DIR=$(cd "$SCRIPT_DIR/.." && pwd)
BUILD_DIR="$PROJECT_DIR/build"

echo -e "${GREEN}=== painttyWidget Qt 6 编译脚本 ===${NC}"
echo ""
echo "Qt 路径:     $QTDIR"
echo "Qt 版本:     $($QMAKE_PATH -query QT_VERSION)"
echo "项目目录:    $PROJECT_DIR"
echo "构建目录:    $BUILD_DIR"
echo ""

CLEAN=0
RELEASE=1
JOBS=$(sysctl -n hw.ncpu)

while [[ $# -gt 0 ]]; do
    case $1 in
        --clean)
            CLEAN=1
            shift
            ;;
        --debug)
            RELEASE=0
            shift
            ;;
        --jobs|-j)
            JOBS="$2"
            shift 2
            ;;
        --help|-h)
            echo "用法: $0 [选项]"
            echo ""
            echo "选项:"
            echo "  --clean      清理后重新编译"
            echo "  --debug      编译 debug 版本"
            echo "  --jobs N     使用 N 个并行任务 (默认: $JOBS)"
            echo "  --help       显示此帮助信息"
            exit 0
            ;;
        *)
            echo -e "${RED}未知选项: $1${NC}"
            exit 1
            ;;
    esac
done

export PATH="$QTDIR/bin:$PATH"

if [ $CLEAN -eq 1 ]; then
    echo -e "${YELLOW}清理旧的构建文件...${NC}"
    cd "$PROJECT_DIR"
    make distclean 2>/dev/null || true
    rm -rf "$BUILD_DIR"
    echo ""
fi

mkdir -p "$BUILD_DIR"
cd "$PROJECT_DIR"

if [ $RELEASE -eq 1 ]; then
    CONFIG="CONFIG+=release"
    echo -e "${GREEN}编译 Release 版本${NC}"
else
    CONFIG="CONFIG+=debug"
    echo -e "${YELLOW}编译 Debug 版本${NC}"
fi

echo ""
echo -e "${GREEN}[1/3] 运行 qmake...${NC}"
$QMAKE_PATH painttyWidget.pro -spec macx-clang $CONFIG sdk_no_version_check

echo ""
echo -e "${GREEN}[2/3] 编译中... (使用 $JOBS 个并行任务)${NC}"
make -j$JOBS

echo ""
echo -e "${GREEN}[3/3] 检查编译结果...${NC}"

MRPAINT_APP="$BUILD_DIR/build/MrPaint.app"
RENDERER_APP="$BUILD_DIR/build/canvas-renderer.app"
RENDERER_WIDGET_APP="$BUILD_DIR/build/renderer-widget.app"

SUCCESS=1

if [ -f "$MRPAINT_APP/Contents/MacOS/MrPaint" ]; then
    SIZE=$(ls -lh "$MRPAINT_APP/Contents/MacOS/MrPaint" | awk '{print $5}')
    echo -e "  ${GREEN}✓${NC} MrPaint.app ($SIZE)"
else
    echo -e "  ${RED}✗${NC} MrPaint.app 未生成"
    SUCCESS=0
fi

if [ -f "$RENDERER_APP/Contents/MacOS/canvas-renderer" ]; then
    echo -e "  ${GREEN}✓${NC} canvas-renderer.app"
else
    echo -e "  ${YELLOW}⚠${NC} canvas-renderer.app 未生成"
fi

if [ -f "$RENDERER_WIDGET_APP/Contents/MacOS/renderer-widget" ]; then
    echo -e "  ${GREEN}✓${NC} renderer-widget.app"
else
    echo -e "  ${YELLOW}⚠${NC} renderer-widget.app 未生成"
fi

echo ""

if [ $SUCCESS -eq 1 ]; then
    echo -e "${GREEN}=== 编译完成 ===${NC}"
    echo ""
    echo "运行应用:"
    echo "  $MRPAINT_APP/Contents/MacOS/MrPaint"
    echo ""
    echo "或者:"
    echo "  open $MRPAINT_APP"
else
    echo -e "${RED}=== 编译失败 ===${NC}"
    exit 1
fi
