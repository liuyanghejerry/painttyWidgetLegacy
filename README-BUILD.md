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
测试使用临时项目和独立的配置命名空间，不写入 Mr.Paint 的用户偏好。

```bash
JOBS=8 ./scripts/test-linux.sh

# 离屏验证示例笔画渲染器
QT_QPA_PLATFORM=offscreen ./build/build/canvas-renderer \
    -i src/renderer/events.json -o build/render-smoke.png
```

项目尺寸上限为 10000×10000 且不超过 64 百万像素，最多 256 个图层。
原生项目文件上限为 512 MiB，读取图层的总像素存储上限为 1 GiB。

## Windows 构建与便携包

Windows x64 使用 Qt 6.6.3 的 `win64_mingw` 和 MinGW 11.2.0，与 Windows 11
虚拟机中的工具链一致。可通过 aqt 安装：

```powershell
python -m pip install aqtinstall==3.3.0
python -m aqt install-qt windows desktop 6.6.3 win64_mingw -O C:\Qt --archives qtbase qttools qttranslations qtsvg MinGW d3dcompiler_47 opengl32sw
python -m aqt install-tool windows desktop tools_mingw90 qt.tools.win64_mingw900 -O C:\Qt

./scripts/build-windows.ps1 -QtDirectory C:\Qt\6.6.3\mingw_64
./scripts/package-windows.ps1 -QtDirectory C:\Qt\6.6.3\mingw_64 -Version dev-local
```

Qt 的工具包标识 `qt.tools.win64_mingw900` 对应 MinGW **11.2.0**，目录为
`C:\Qt\Tools\mingw1120_64`。脚本会检查 Qt 和编译器版本，并在任何构建或测试失败时退出。

三个程序在 `build/windows/build/` 中，回归测试结果在 `build/windows/test-results.txt`
和 `test-results.xml` 中。发行 ZIP 和 SHA-256 文件位于 `dist/`，ZIP 包含 `MrPaint.exe`、
Qt DLL、插件、MinGW 运行库、许可文本和记录版本与源提交的 `build-info.json`。
用户解压整个 ZIP 后运行 `MrPaint.exe`，无需安装 Qt。

打包脚本使用 `windeployqt` 部署依赖，并解压生成的 ZIP，在清除 SDK 路径后执行
`MrPaint.exe --version`，检查包能独立启动。本地 Windows 11 验证中，12 项回归测试全部通过。

## GitHub Actions Windows 发行

`.github/workflows/ci.yml` 在主开发分支、`feature/**` 分支的提交和针对主开发分支的
Pull Request 上构建、测试并打包 Windows x64 版本，也支持手动运行。
在 Actions 运行页的 **Artifacts** 中下载：

- `windows-portable`：`MrPaint-dev-<提交短哈希>-windows-x64.zip` 及 `.zip.sha256` 校验文件。
- `windows-test-results`：文本和 JUnit 测试结果，以及便携包启动检查日志。

构建产物保留 30 天。普通分支构建不创建 Release。

推送版本标签后，Linux、macOS、Windows CI 全部成功才自动发布 GitHub Release：

```bash
# 在需要发行的提交上创建版本标签，例如：
git tag v0.6.0
git push origin v0.6.0
```

发行附件为 `MrPaint-v0.6.0-windows-x64.zip` 和 `.zip.sha256`。含连字符的版本标签
（例如 `v0.6.0-beta.1`）会标记为预发行。工作流核对校验值、创建草稿并上传附件，
完成后才公开；重跑同一标签的工作流会更新附件。只有标签发布任务拥有仓库写入权限。
若使用手动触发，需要此工作流先存在于仓库默认分支。

## macOS

使用对应 Qt 6.6.3 的 qmake 进行目录外构建：

```bash
mkdir -p build
cd build
/path/to/Qt/bin/qmake -r ../painttyWidget.pro CONFIG+=release
make -j8
```

macOS 构建需要在对应系统验证。
