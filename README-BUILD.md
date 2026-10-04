# Mr.Paint 本地绘画版：构建与使用

本项目是离线单人绘画软件，使用 Qt 6.6.3、C++17 和 qmake。
桌面程序仅依赖 Qt Core、Gui、Widgets，不需要服务端或 Qt Network。

## Linux 构建

使用 aqt 安装 Qt 和系统依赖的完整步骤见
[Qt 开发环境配置指南](docs/Qt6-Setup-Guide.md#linuxubuntu--debian)。

```bash
./scripts/build-linux.sh --jobs 8
./build/build/mrpaint

# 使用其他 Qt 安装路径或调试配置
QTDIR="$HOME/develop/Qt/6.6.3/gcc_64" ./scripts/build-linux.sh --debug --jobs 8
```

三个构建产物位于 `build/build/`：`mrpaint`、`canvas-renderer`、`renderer-widget`。
构建目录及生成文件不会加入版本控制。

## 绘画与项目

启动后直接进入 1280×720 的空白画布，并创建一个透明图层。

- **新建 / 打开 / 保存 / 另存为**：文件菜单和工具栏；Linux 快捷键分别为
  Ctrl+N、Ctrl+O、Ctrl+S、Ctrl+Shift+S。
- **项目文件**：`.paintty` 是一个独立的可编辑文件，保存画布尺寸、所有图层的
  像素、名称、顺序、显示状态、锁定状态及选中图层。空图层也会保留。
- **保存保护**：原子写入成功后才替换旧文件；保存失败不会删除之前的项目。
  新建、打开和关闭时会询问如何处理未保存内容，取消或保存失败会中止操作。
- **撤销 / 重做**：Ctrl+Z / Ctrl+Shift+Z（平台可能也支持 Ctrl+Y），覆盖笔画和图层编辑。
  会话历史最多保留 30 次操作，估算图像存储上限为 256 MiB；超限时清空旧历史。
  历史不写入项目文件。
- **图层**：添加、删除、上移、下移、清空，以及双击名称重命名。
  点击显示和锁定图标切换状态；锁定或隐藏图层不能绘画，清空全部时跳过锁定图层。
- **导入图片**：文件菜单“Import Image as Layer…”将图片放入新的图层，保持原始大小，
  超出画布的部分会被裁切。
- **导出**：PNG、PSD 和剪贴板。Visible 导出合成当前显示的图层，All 导出包含隐藏图层。
  `.paintty` 项目是完整保留图层属性的编辑格式；PSD 导出沿用旧版的图像图层导出器。
- **最近项目**：文件菜单“Open Recent”保留最近 10 个本地项目。
- **数位板**：偏好设置中的 Drawing 页可启用或关闭数位板，物理鼠标也可以继续绘画。
  Pressure Brush 支持压感笔画。

也可以直接打开项目：

```bash
./build/build/mrpaint /path/to/drawing.paintty
```

旧版 `.paintty` 文件夹通过“Open Legacy Project Folder…”或命令行打开，另存为新格式后
可以迁移，原文件夹保留不变。旧格式没有记录图层名称、锁定、显示状态，因此这些信息
无法从旧项目恢复。

设置保存在系统的用户配置目录：Linux 默认为 `~/.config/Paintty/MrPaint/mrpaint.ini`。
首次运行会导入工作目录中已有的本地偏好；旧的服务器与重放设置不会被导入。

## 验证

QtTest 回归测试覆盖图层往返保存、覆盖失败保护、损坏文件、旧格式层序、撤销重做、
未保存关闭提示、取消另存为、鼠标和 Pressure Brush 笔画、图层锁定与本地偏好。
测试使用临时项目及配置目录，不写入用户偏好。

```bash
JOBS=8 ./scripts/test-linux.sh

# 离屏验证示例笔画渲染器
QT_QPA_PLATFORM=offscreen ./build/build/canvas-renderer \
    -i src/renderer/events.json -o build/render-smoke.png
```

项目尺寸上限为 10000×10000 且不超过 64 百万像素，最多 256 个图层。
原生项目文件上限为 512 MiB，读取图层的总像素存储上限为 1 GiB。

## macOS / Windows

使用对应 Qt 6.6.3 的 qmake 进行目录外构建：

```bash
mkdir -p build
cd build
/path/to/Qt/bin/qmake -r ../painttyWidget.pro CONFIG+=release
make -j8
```

Windows 在配置了 MSVC 和 Qt 的开发者命令行中使用 `nmake` 替代 `make`。
本次验证平台为 Linux，其他平台需要在对应系统验证。
