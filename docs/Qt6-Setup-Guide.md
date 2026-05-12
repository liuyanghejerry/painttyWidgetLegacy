# Qt 6 开发环境配置指南

本文档记录了在 macOS 上使用 aqt (Another Qt installer) 安装 Qt 6.6.3 并编译 painttyWidget 项目的过程。

## 环境信息

| 项目 | 值 |
|------|-----|
| 操作系统 | macOS (darwin) |
| SDK 版本 | macOS SDK 26.2 |
| Qt 版本 | 6.6.3 |
| 架构 | clang_64 (arm64 + x86_64 universal) |
| 编译器 | Apple Clang 17.0.0 |

## Qt 安装

### 安装路径

```
/Users/liuyanghejerry/develop/qt/6.6.3/macos/
```

### 目录结构

```
6.6.3/macos/
├── bin/           # Qt 工具 (qmake, moc, uic 等)
├── include/       # 头文件
├── lib/           # 库文件 (frameworks)
├── libexec/       # 辅助工具
├── mkspecs/       # qmake 配置文件
├── modules/       # 模块配置
├── plugins/       # Qt 插件
└── translations/  # 翻译文件
```

### 使用的 Qt 模块

项目使用以下 Qt 模块：

- `QtCore` - 核心模块
- `QtGui` - GUI 模块
- `QtWidgets` - 控件模块
- `QtNetwork` - 网络模块
- `QtConcurrent` - 并发模块

## 已知问题及解决方案

### 1. AGL Framework 缺失

**问题描述**：macOS SDK 26.2 移除了 AGL (Apple OpenGL) framework，但 Qt 6.6.3 的库文件中仍然引用了它。

**错误信息**：
```
ld: framework 'AGL' not found
```

**解决方案**：

1. 创建 stub AGL framework：
```bash
# 创建 framework 目录结构
mkdir -p /Users/liuyanghejerry/develop/qt/6.6.3/macos/lib/AGL.framework/Versions/A

# 创建符号链接
cd /Users/liuyanghejerry/develop/qt/6.6.3/macos/lib/AGL.framework/Versions
ln -s A Current
cd ..
ln -s Versions/Current/AGL AGL
ln -s Versions/Current/Headers Headers
ln -s Versions/Current/Resources Resources

# 创建空的 stub 库
cd /tmp
echo "void agl_stub(void) {}" > agl_stub.c
/usr/bin/cc -c agl_stub.c -o agl_stub.o
/usr/bin/ar rcs /Users/liuyanghejerry/develop/qt/6.6.3/macos/lib/AGL.framework/Versions/A/AGL agl_stub.o
rm -f agl_stub.c agl_stub.o
```

2. 修改 Qt 配置文件（可选，已修改）：
   - `mkspecs/common/mac.conf` - 移除 AGL 引用
   - `mkspecs/modules/qt_lib_gui_private.pri` - 移除 AGL 引用

### 2. SDK 版本不匹配警告

**问题描述**：Qt 6.6.3 测试的 SDK 版本是 14，而当前系统使用 26.2。

**警告信息**：
```
Project WARNING: Qt has only been tested with version 14 of the platform SDK, you're using 26.
```

**解决方案**：在 qmake 命令中添加 `sdk_no_version_check` 配置：
```bash
qmake CONFIG+=release sdk_no_version_check
```

### 3. SIMD 不支持

**警告信息**：
```
Project WARNING: SIMD not supported on macOS arm64
```

**说明**：这是正常的，ARM64 架构不支持项目中使用的 x86 SIMD 指令。项目会回退到标量实现。

## 编译命令

### 基本编译流程

```bash
# 设置 Qt 环境
export QTDIR=/Users/liuyanghejerry/develop/qt/6.6.3/macos
export PATH=$QTDIR/bin:$PATH

# 进入项目目录
cd /Users/liuyanghejerry/develop/painttyWidget

# 清理旧的构建文件（可选）
make distclean 2>/dev/null || true

# 运行 qmake
qmake painttyWidget.pro -spec macx-clang CONFIG+=release sdk_no_version_check

# 编译
make -j$(sysctl -n hw.ncpu)
```

### 编译产物

| 产物 | 路径 |
|------|------|
| MrPaint.app | `build/build/MrPaint.app/` |
| 可执行文件 | `build/build/MrPaint.app/Contents/MacOS/MrPaint` |
| canvas-renderer | `build/build/canvas-renderer.app/` |
| renderer-widget | `build/build/renderer-widget.app/` |

## 使用编译脚本

项目提供了便捷的编译脚本：

```bash
# 使用脚本编译（需要设置 QTDIR 环境变量）
./scripts/build-qt6.sh

# 或者指定 Qt 路径
QTDIR=/Users/liuyanghejerry/develop/qt/6.6.3/macos ./scripts/build-qt6.sh
```

## aqt 常用命令参考

```bash
# 列出可用的 Qt 版本
python3 -m aqt list-qt mac desktop

# 列出特定版本的可用架构
python3 -m aqt list-qt mac desktop --arch 6.6.3

# 列出特定版本的可用模块
python3 -m aqt list-qt mac desktop --modules 6.6.3 clang_64

# 安装 Qt
python3 -m aqt install-qt mac desktop <version> <arch> -O <output_dir>

# 安装额外模块
python3 -m aqt install-qt mac desktop <version> <arch> -m <module> -O <output_dir>
```

## 故障排除

### qmake 找不到

确保 `$QTDIR/bin` 在 PATH 中：
```bash
export QTDIR=/Users/liuyanghejerry/develop/qt/6.6.3/macos
export PATH=$QTDIR/bin:$PATH
which qmake
```

### 链接错误

如果遇到其他 framework 缺失错误，可以使用与 AGL 相同的方法创建 stub framework。

### 清理构建

```bash
# 完全清理
make distclean

# 删除构建目录
rm -rf build/
```

## 参考资料

- [aqtinstall 文档](https://github.com/miurahr/aqtinstall)
- [Qt 6 官方文档](https://doc.qt.io/qt-6/)
- [Qt 6 支持的 macOS 版本](https://doc.qt.io/qt-6/supported-platforms.html)

---

*最后更新: 2025-05-12*
