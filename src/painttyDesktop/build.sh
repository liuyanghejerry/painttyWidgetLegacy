#!/bin/bash

# PainttyWidget SSE 客户端构建脚本
# 支持 Linux、macOS 和 Windows (通过 WSL)

set -e  # 遇到错误时退出

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 获取脚本所在目录（绝对路径）
SCRIPT_DIR=$(realpath "$(dirname "$0")")

# 获取项目根目录
PROJECT_ROOT=$(realpath "$(dirname "$SCRIPT_DIR")")

# 定义各种路径
BUILD_DIR="$PROJECT_ROOT/../build"
DEB_PACKAGE_DIR="$SCRIPT_DIR/deb_package"
PROJECT_FILE="$SCRIPT_DIR/painttyDesktop.pro"
MAKEFILE_PATH="$BUILD_DIR/Makefile"
RUN_LOG_PATH="$PROJECT_ROOT/run.log"

# 打印带颜色的消息
print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# 检查依赖
check_dependencies() {
    print_info "检查构建依赖..."
    
    # 检查 qmake
    if ! command -v qmake &> /dev/null; then
        print_error "qmake 未找到，请安装 Qt 开发环境"
        exit 1
    fi
    
    # 检查 make
    if ! command -v make &> /dev/null; then
        print_error "make 未找到，请安装构建工具"
        exit 1
    fi
    
    # 检查 lupdate
    if ! command -v lupdate &> /dev/null; then
        print_error "lupdate 未找到，请安装 Qt 语言工具"
        exit 1
    fi
    
    # 检查 lrelease
    if ! command -v lrelease &> /dev/null; then
        print_error "lrelease 未找到，请安装 Qt 语言工具"
        exit 1
    fi
    
    # 检查 Qt 版本
    QT_VERSION=$(qmake -query QT_VERSION)
    print_info "检测到 Qt 版本: $QT_VERSION"
    
    print_success "依赖检查完成"
}

# 清理构建目录
clean_build() {
    print_info "清理构建目录..."
    
    if [ -d "$BUILD_DIR" ]; then
        rm -rf "$BUILD_DIR"
        print_success "构建目录已清理"
    fi
    
    if [ -d "$DEB_PACKAGE_DIR" ]; then
        rm -rf "$DEB_PACKAGE_DIR"
        print_success "Debian 包目录已清理"
    fi
}

# 生成翻译文件
generate_translations() {
    print_info "生成翻译文件..."
    
    # 确保在正确的目录中运行翻译工具
    cd "$SCRIPT_DIR" || exit 1
    
    # 更新翻译源文件 (.ts)
    print_info "更新翻译源文件..."
    lupdate painttyDesktop.pro
    
    if [ $? -eq 0 ]; then
        print_success "翻译源文件更新成功"
    else
        print_error "翻译源文件更新失败"
        exit 1
    fi
    
    # 编译翻译文件 (.qm)
    print_info "编译翻译文件..."
    lrelease painttyDesktop.pro
    
    if [ $? -eq 0 ]; then
        print_success "翻译文件编译成功"
    else
        print_error "翻译文件编译失败"
        exit 1
    fi
    
    # 返回到原始目录
    cd - > /dev/null || exit 1
}

# 配置项目
configure_project() {
    print_info "配置项目..."
    
    # 创建构建目录
    mkdir -p "$BUILD_DIR"
    
    # 运行 qmake
    qmake "$PROJECT_FILE" -o "$MAKEFILE_PATH"
    
    if [ $? -eq 0 ]; then
        print_success "项目配置成功"
    else
        print_error "项目配置失败"
        exit 1
    fi
}

# 编译项目
compile_project() {
    print_info "编译项目..."
    
    cd "$BUILD_DIR"
    
    # 获取 CPU 核心数
    CPU_CORES=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
    print_info "使用 $CPU_CORES 个 CPU 核心进行编译"
    
    # 编译
    make -j$CPU_CORES
    
    if [ $? -eq 0 ]; then
        print_success "编译成功"
    else
        print_error "编译失败"
        exit 1
    fi
    
    cd "$SCRIPT_DIR"
}

# 显示帮助信息
show_help() {
    echo "PainttyWidget SSE 客户端构建脚本"
    echo ""
    echo "用法: $0 [选项]"
    echo ""
    echo "选项:"
    echo "  --clean        清理构建目录"
    echo "  --run          构建后自动运行客户端"
    echo "  --translations 仅生成翻译文件"
    echo "  --help         显示此帮助信息"
    echo ""
    echo "示例:"
    echo "  $0                    # 构建项目"
    echo "  $0 --clean           # 清理后构建"
    echo "  $0 --run             # 构建并运行客户端"
    echo "  $0 --translations    # 仅生成翻译文件"
    echo ""
    echo "环境要求:"
    echo "  - Qt 6.0 或更高版本"
    echo "  - 支持 C++17 的编译器"
    echo "  - make 构建工具"
    echo "  - Qt 语言工具 (lupdate, lrelease)"
}

# 主函数
main() {
    print_info "开始构建 PainttyWidget SSE 客户端..."
    
    # 解析命令行参数
    CLEAN_BUILD=false
    RUN_AFTER_BUILD=false
    TRANSLATIONS_ONLY=false
    
    for arg in "$@"; do
        case $arg in
            --clean)
                CLEAN_BUILD=true
                shift
                ;;
            --run)
                RUN_AFTER_BUILD=true
                shift
                ;;
            --translations)
                TRANSLATIONS_ONLY=true
                shift
                ;;
            --help)
                show_help
                exit 0
                ;;
            *)
                print_error "未知参数: $arg"
                show_help
                exit 1
                ;;
        esac
    done
    
    # 检查依赖
    check_dependencies
    
    # 如果只需要生成翻译文件
    if [ "$TRANSLATIONS_ONLY" = true ]; then
        generate_translations
        print_success "翻译文件生成完成！"
        exit 0
    fi
    
    # 清理构建目录（如果需要）
    if [ "$CLEAN_BUILD" = true ]; then
        clean_build
    fi
    
    # 生成翻译文件
    generate_translations
    
    # 配置项目
    configure_project
    
    # 编译项目
    compile_project
    
    print_success "构建完成！"
    print_info "可执行文件位置: $BUILD_DIR"
    
    if [ "$RUN_AFTER_BUILD" = true ]; then
        print_info "正在启动 PainttyWidget 客户端..."
        exe_path=$(find "$BUILD_DIR" -type f \( -name 'MrPaint' -o -name 'mrpaint' \) -perm +111 | head -n 1)
        print_info "exe_path: $exe_path"
        if [ -n "$exe_path" ]; then
            print_info "日志将重定向到 $RUN_LOG_PATH 文件"
            "$exe_path" > "$RUN_LOG_PATH" 2>&1
        else
            print_error "未找到可执行文件！"
            exit 1
        fi
    fi
}

# 运行主函数
main "$@" 