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
- **鼠标绘画**：仅接受鼠标输入，保留基础画笔、橡皮、二值画笔、蜡笔和素描笔；
  不提供数位板、压感或倾角控制。鼠标移动逐点即时绘制；取色、平移、换笔或切出窗口
  时先结束当前笔画并记录撤销。
- **工具快捷键**：B 基础画笔，N / E 橡皮，M 二值画笔，逗号蜡笔，句号素描笔；
  按一下即保持选中。按住 V 临时取色，按住 C / 空格临时平移，松开恢复之前的工具；
  工具栏上的取色、平移按钮也可点击保持选中。E 和空格是默认快捷键的兼容别名，
  自定义快捷键优先。输入框内正常输入不会切换工具。
- **画布操作**：右键拖动或移动工具平移，旋转和缩放后拖动仍跟随鼠标。
  `=` / `-` 或 Ctrl+滚轮按相同比例放大、缩小；`[` / `]` 旋转，`\` 恢复 100%、
  零旋转并居中。Q / W 调整笔宽，A / S 调整硬度，Z / X 调整浓度。

也可以直接打开项目：

```bash
./build/build/mrpaint /path/to/drawing.paintty
```

旧版 `.paintty` 文件夹通过“Open Legacy Project Folder…”或命令行打开，另存为新格式后
可以迁移，原文件夹保留不变。旧格式没有记录图层名称、锁定、显示状态，因此这些信息
无法从旧项目恢复。

设置保存在系统的用户配置目录：Linux 默认为 `~/.config/Paintty/MrPaint/mrpaint.ini`。
首次运行会导入工作目录中已有的本地偏好；旧的服务器与重放设置不会被导入。

## 界面翻译

偏好设置中可选择系统默认、English、简体中文、繁體中文或日本語。
语言修改在下次启动时生效。应用翻译及 Qt 标准按钮、文件对话框等控件的翻译
都嵌入程序资源，便携包无需额外安装语言文件。

翻译源文件为 `src/painttyDesktop/translation/paintty_*.ts`，对应的 `.qm` 编译文件
也随源码维护。新增或修改界面文案时，在项目根目录执行：

```bash
export QTDIR="$HOME/develop/Qt/6.6.3/gcc_64"
"$QTDIR/bin/lupdate" src/painttyDesktop/painttyDesktop.pro -no-obsolete -locations absolute
# 用 Qt Linguist 或文本编辑器补齐 .ts 词条，再生成程序实际使用的文件：
"$QTDIR/bin/lrelease" -nounfinished src/painttyDesktop/translation/paintty_*.ts
./scripts/build-linux.sh --jobs 8
```

Windows 使用同一套 Qt Linguist 工具（`lupdate.exe`、`lrelease.exe`）。
Linux 的 `lupdate` 工具还需要 Qt 的 `qtdeclarative` 安装包，应用本身仍只使用
Core、Gui、Widgets。内置的 `qt_*.qm` 来自 Qt 6.6.3 的 `qtbase_*.qm`，覆盖
这三个模块；更新 Qt 版本时应同步替换，以保持标准控件翻译与 SDK 一致。

## 验证

QtTest 回归测试覆盖图层往返保存、覆盖失败保护、损坏文件、旧格式层序、撤销重做、
未保存关闭提示、取消另存为、鼠标笔画和非鼠标兼容事件过滤、图层锁定与本地偏好，
以及旧窗口布局恢复后的工具栏拖动、高 DPI 导航预览居中和点击定位。
交互测试还覆盖按键交叠、修饰键变化、窗口失焦、文字输入、笔画中途切换工具、
即时采样和转折点保留，以及多种缩放和旋转组合下的逐像素平移与缩放往返。
测试使用临时项目和独立的配置命名空间，不写入 Mr.Paint 的用户偏好。

```bash
JOBS=8 ./scripts/test-linux.sh

# 离屏验证独立历史笔画渲染器（仅用于旧事件样本，不接入桌面绘画）
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
`MrPaint.exe --version`，检查包能独立启动且版本与源码一致。Windows CI 会运行同一套回归测试。

## GitHub Actions Windows 发行

`.github/workflows/ci.yml` 在主开发分支、`feature/**` 分支的提交和针对主开发分支的
Pull Request 上构建、测试并打包 Windows x64 版本，也支持手动运行。
在 Actions 运行页的 **Artifacts** 中下载：

- `windows-portable`：`MrPaint-dev-<提交短哈希>-windows-x64.zip` 及 `.zip.sha256` 校验文件。
- `windows-test-results`：文本和 JUnit 测试结果，以及便携包启动检查日志。

构建产物保留 30 天。普通分支构建不创建 Release。

推送版本标签后，Linux、macOS、Windows CI 全部成功才自动发布 GitHub Release：

应用版本统一维护在根目录 `VERSION`，用于命令行、“关于”窗口和 Windows EXE 版本信息。
发行标签须与该文件一致（例如 `VERSION` 为 `1.0.0` 时使用 `v1.0.0`）。

```bash
# 在需要发行的提交上创建版本标签，例如：
git tag v1.0.0
git push origin v1.0.0
```

发行附件为 `MrPaint-v1.0.0-windows-x64.zip` 和 `.zip.sha256`。含连字符的版本标签
（例如 `v1.0.0-beta.1`）会标记为预发行。工作流核对校验值、创建草稿并上传附件，
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
