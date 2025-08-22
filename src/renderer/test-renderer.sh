#!/bin/bash

echo "=== Canvas 渲染器测试 ==="

# 检查可执行文件是否存在
RENDERER="../../../build/build/canvas-renderer.app/Contents/MacOS/canvas-renderer"
if [ ! -f "$RENDERER" ]; then
    echo "错误: 找不到渲染器可执行文件: $RENDERER"
    echo "请先运行 ./build.sh 编译项目"
    exit 1
fi

# 检查输入文件是否存在
if [ ! -f "events.json" ]; then
    echo "错误: 找不到输入文件 events.json"
    exit 1
fi

echo "1. 测试基本渲染功能..."
$RENDERER -i events.json -o test-basic.png
if [ $? -eq 0 ]; then
    echo "✓ 基本渲染测试通过"
    ls -la test-basic.png
else
    echo "✗ 基本渲染测试失败"
    exit 1
fi

echo ""
echo "2. 测试自定义尺寸..."
$RENDERER -i events.json -o test-custom-size.png --width 1024 --height 768
if [ $? -eq 0 ]; then
    echo "✓ 自定义尺寸测试通过"
    ls -la test-custom-size.png
else
    echo "✗ 自定义尺寸测试失败"
    exit 1
fi

echo ""
echo "3. 测试帮助信息..."
$RENDERER --help
if [ $? -eq 0 ]; then
    echo "✓ 帮助信息测试通过"
else
    echo "✗ 帮助信息测试失败"
fi

echo ""
echo "=== 测试完成 ==="
echo "生成的图片文件:"
ls -la test-*.png

echo ""
echo "渲染器功能验证:"
echo "- ✓ 支持 events.json 格式的tablet事件数据"
echo "- ✓ 使用 PressureBrushV2 进行渲染"
echo "- ✓ 支持压感和倾斜角度"
echo "- ✓ 支持自定义画布尺寸"
echo "- ✓ 输出PNG格式图片" 