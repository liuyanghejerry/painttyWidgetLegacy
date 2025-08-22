#!/bin/bash

set -e

# 进入项目目录
cd "$(dirname "$0")"

# 清理之前的构建
rm -rf *.o *.moc Makefile

# 检查qmake是否可用
if ! command -v qmake &> /dev/null; then
    echo "错误: qmake 未找到，请检查 Qt 安装"
    exit 1
fi

# 运行qmake
qmake renderer-widget.pro

# 编译
make

echo "构建完成！"
echo "可执行文件位置: ../../../build/build/renderer-widget" 

if [[ "$1" == "--run" ]]; then
    ../../../build/build/renderer-widget.app/Contents/MacOS/renderer-widget
fi