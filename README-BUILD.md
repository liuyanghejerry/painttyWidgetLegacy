# PainttyWidget SSE 客户端构建指南

本文档详细说明了如何通过命令行构建 PainttyWidget SSE 客户端项目。

## 系统要求

### 基本要求
- **操作系统**: Linux、macOS 或 Windows (通过 WSL)
- **编译器**: 支持 C++17 的编译器 (GCC 7+, Clang 5+, MSVC 2017+)
- **Qt 版本**: Qt 6.0 或更高版本
- **构建工具**: make 或 ninja

### Qt 模块要求
- `core` - Qt 核心模块
- `gui` - Qt GUI 模块
- `network` - Qt 网络模块
- `widgets` - Qt 控件模块
- `qml` - Qt QML 模块 (替代 script)
- `concurrent` - Qt 并发模块

## 安装依赖

### Ubuntu/Debian
```bash
# 安装 Qt6 开发环境
sudo apt update
sudo apt install qt6-base-dev qt6-tools-dev qt6-qml-dev

# 安装构建工具
sudo apt install build-essential cmake

# 安装额外的 Qt6 模块
sudo apt install libqt6concurrent6
```

### CentOS/RHEL/Fedora
```bash
# 安装 Qt6 开发环境 (Fedora)
sudo dnf install qt6-qtbase-devel qt6-qttools-devel 
```

### macOS
```bash
# 使用 Homebrew 安装 Qt6
brew install qt6

# 或者使用官方 Qt 安装包
# 从 https://www.qt.io/download 下载并安装
```

### Windows
```bash
# 使用 vcpkg
vcpkg install qt6-base qt6-tools

# 或者使用官方 Qt 安装包
# 从 https://www.qt.io/download 下载并安装
```

## 构建方法

### 方法一：使用构建脚本（推荐）

1. **克隆项目**
```bash
git clone <repository-url>
cd painttyWidget
```

2. **设置执行权限**
```bash
chmod +x build.sh
```

3. **运行构建脚本**
```bash
# 基本构建
./build.sh

# 清理后构建
./build.sh --clean

# 构建并安装
./build.sh --install

# 构建并运行测试
./build.sh --test

# 查看帮助
./build.sh --help
```

### 方法二：手动构建

1. **检查 Qt 环境**
```bash
# 检查 qmake 是否可用
qmake --version

# 检查 Qt 版本
qmake -query QT_VERSION
```

2. **配置项目**
```bash
# 创建构建目录
mkdir build
cd build

# 生成 Makefile
qmake ../painttyWidget.pro
```

3. **编译项目**
```bash
# 编译（使用所有 CPU 核心）
make -j$(nproc)

# 或者指定核心数
make -j4
```

4. **安装（可选）**
```bash
# Linux 系统
sudo make install

# 其他系统
make install
```

### 方法三：使用 CMake（如果支持）

```bash
mkdir build
cd build
cmake ..
make -j$(nproc)
```

## 构建选项

### 环境变量
```bash
# 指定 Qt 安装路径
export QTDIR=/usr/lib/qt5

# 指定编译器
export CC=gcc
export CXX=g++

# 指定构建类型
export CONFIG=release
```

### qmake 参数
```bash
# 指定 Qt 版本
qmake -qt=5 painttyWidget.pro

# 指定构建配置
qmake CONFIG+=release painttyWidget.pro

# 指定安装路径
qmake PREFIX=/usr/local painttyWidget.pro
```

## 平台特定说明

### Linux
- 目标文件: `build/mrpaint`
- 安装路径: `/usr/bin/mrpaint`
- 依赖库: 自动处理

### macOS
- 目标文件: `build/MrPaint.app`
- 安装路径: `/Applications/MrPaint.app`
- 图标: `iconset/icon.icns`

### Windows
- 目标文件: `build/MrPaint.exe`
- 安装路径: `C:\Program Files\MrPaint\`
- 资源文件: `app.rc`

## 故障排除

### 常见错误

#### 1. qmake 未找到
```bash
# 错误信息
qmake: command not found

# 解决方案
# Ubuntu/Debian
sudo apt install qt5-default

# 或者设置 PATH
export PATH=/usr/lib/qt5/bin:$PATH
```

#### 2. Qt 模块未找到
```bash
# 错误信息
Project ERROR: Unknown module(s) in QT: network

# 解决方案
# Ubuntu/Debian
sudo apt install qt5-qtbase-dev qt5-qttools-dev

# 检查模块安装
qmake -query QT_INSTALL_LIBS
```

#### 3. 编译器不支持 C++11
```bash
# 错误信息
error: 'auto' keyword not supported

# 解决方案
# 更新编译器
sudo apt install gcc-8 g++-8

# 或者设置编译器
export CC=gcc-8
export CXX=g++-8
```

#### 4. 缺少头文件
```bash
# 错误信息
fatal error: 'QNetworkAccessManager' file not found

# 解决方案
# 安装 Qt 网络模块
sudo apt install qt5-qtbase-dev
```

### 调试构建

#### 启用详细输出
```bash
# qmake 详细输出
qmake -d painttyWidget.pro

# make 详细输出
make VERBOSE=1
```

#### 检查依赖
```bash
# 检查 Qt 安装
qmake -query

# 检查库依赖
ldd build/mrpaint

# 检查头文件路径
pkg-config --cflags Qt5Core Qt5Widgets Qt5Network
```

## 测试构建结果

### 基本测试
```bash
# 检查可执行文件
ls -la build/mrpaint

# 运行程序
./build/mrpaint

# 检查版本信息
./build/mrpaint --version
```

### SSE 客户端测试
```bash
# 编译测试程序
g++ -std=c++11 \
    -I/usr/include/qt5 \
    -I/usr/include/qt5/QtCore \
    -I/usr/include/qt5/QtWidgets \
    -I/usr/include/qt5/QtNetwork \
    -lQt5Core -lQt5Widgets -lQt5Network \
    src/painttyDesktop/test-sse-integration.cpp \
    -o test_sse

# 运行测试
./test_sse
```

## 部署

### 创建发布包

#### Linux (Debian/Ubuntu)
```bash
# 构建 Debian 包
cd build
make package

# 安装包
sudo dpkg -i mrpaint_*.deb
```

#### macOS
```bash
# 创建 DMG 包
cd build
macdeployqt MrPaint.app -dmg
```

#### Windows
```bash
# 创建安装包
cd build
windeployqt MrPaint.exe
```

### 分发
```bash
# 创建发布目录
mkdir release
cp build/mrpaint release/
cp -r iconset release/
cp README.md release/

# 创建压缩包
tar -czf painttywidget-sse-$(date +%Y%m%d).tar.gz release/
```

## 持续集成

### GitHub Actions 示例
```yaml
name: Build PainttyWidget SSE

on: [push, pull_request]

jobs:
  build:
    runs-on: ubuntu-latest
    
    steps:
    - uses: actions/checkout@v2
    
    - name: Install Qt
      uses: jurplel/install-qt-action@v3
      with:
        version: '5.15.2'
    
    - name: Build
      run: |
        chmod +x build.sh
        ./build.sh --clean
    
    - name: Test
      run: |
        ./build.sh --test
```

## 性能优化

### 编译优化
```bash
# 启用优化
qmake CONFIG+=release painttyWidget.pro

# 启用链接时优化
qmake CONFIG+=ltcg painttyWidget.pro

# 启用调试信息
qmake CONFIG+=debug painttyWidget.pro
```

### 运行时优化
```bash
# 设置环境变量
export QT_LOGGING_RULES="*.debug=false"
export QT_QUICK_BACKEND=software
```

## 维护

### 清理构建文件
```bash
# 清理所有构建文件
make clean
make distclean

# 或者使用脚本
./build.sh --clean
```

### 更新依赖
```bash
# 更新 Qt
sudo apt update && sudo apt upgrade qt5-default

# 更新构建工具
sudo apt update && sudo apt upgrade build-essential
```

## 贡献

### 开发环境设置
```bash
# 克隆开发分支
git clone -b develop <repository-url>

# 安装开发依赖
sudo apt install clang-format cppcheck

# 设置代码格式化
clang-format -i src/**/*.cpp src/**/*.h
```

### 提交前检查
```bash
# 运行构建测试
./build.sh --clean --test

# 检查代码风格
cppcheck --enable=all src/

# 运行单元测试
make test
```

## 联系支持

如果遇到构建问题，请：

1. 检查系统要求是否满足
2. 查看故障排除部分
3. 查看项目 Issues
4. 提交详细的错误报告

---

**注意**: 本文档适用于 PainttyWidget SSE 客户端版本。如果使用其他版本，请参考相应的文档。 