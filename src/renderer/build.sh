#!/bin/bash

# Canvas 渲染器编译脚本

set -e

export PATH=$PATH:/Users/liuyanghejerry/develop/Qt/6.8.2/arm64/bin


echo "开始编译 Canvas 渲染器..."

# 检查Qt环境
if ! command -v qmake &> /dev/null; then
    echo "错误: 未找到 qmake，请确保Qt环境已正确安装"
    exit 1
fi

# 创建输出目录
mkdir -p ../../../build/build

# 编译简单渲染器
echo "编译简单渲染器..."
qmake renderer.pro
make clean
make

if [ $? -eq 0 ]; then
    echo "简单渲染器编译成功！"
    echo "可执行文件位置: ../../../build/build/canvas-renderer"
else
    echo "简单渲染器编译失败！"
    exit 1
fi

# 清理临时文件
make clean
rm -f Makefile

echo ""
echo "编译完成！"
echo ""
echo "使用方法:"
echo "  ./canvas-renderer -i example.json -o output.png"
echo "  ./canvas-renderer --help"
echo ""
echo "示例:"
echo "  ./canvas-renderer -i example.json -o example.png" 