#!/bin/sh

# C/C++代码格式化脚本
# 使用.clang-format配置文件格式化当前目录下所有的C/C++源文件

set -e  # 遇到错误立即退出

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 打印带颜色的消息
print_info() {
    if [ -t 1 ]; then
        printf "${BLUE}[INFO]${NC} %s\n" "$1"
    else
        printf "[INFO] %s\n" "$1"
    fi
}

print_success() {
    if [ -t 1 ]; then
        printf "${GREEN}[SUCCESS]${NC} %s\n" "$1"
    else
        printf "[SUCCESS] %s\n" "$1"
    fi
}

print_warning() {
    if [ -t 1 ]; then
        printf "${YELLOW}[WARNING]${NC} %s\n" "$1"
    else
        printf "[WARNING] %s\n" "$1"
    fi
}

print_error() {
    if [ -t 1 ]; then
        printf "${RED}[ERROR]${NC} %s\n" "$1"
    else
        printf "[ERROR] %s\n" "$1"
    fi
}

# 检查clang-format是否安装
check_clang_format() {
    if [ ! -x "/usr/bin/clang-format" ] && [ ! -x "$(which clang-format 2>/dev/null)" ]; then
        print_error "clang-format 未安装！"
        echo "请安装 clang-format："
        echo "  Ubuntu/Debian: sudo apt-get install clang-format"
        echo "  CentOS/RHEL:   sudo yum install clang-format"
        echo "  macOS:         brew install clang-format"
        exit 1
    fi

    local version=$(clang-format --version | head -n1)
    print_info "使用 $version"
}

# 向上寻找可用的 .clang-format
find_clang_format() {
    local dir=$(pwd)
    while :; do
        if [ -f "$dir/.clang-format" ]; then
            echo "$dir/.clang-format"
            return 0
        fi
        if [ "$dir" = "/" ]; then
            break
        fi
        dir=$(dirname "$dir")
    done
    return 1
}

# 检查.clang-format文件是否存在（包含向上查找）
check_config_file() {
    local cfg
    if cfg=$(find_clang_format); then
        if [ "$cfg" = "$(pwd)/.clang-format" ]; then
            print_success "找到 .clang-format 配置文件"
        else
            print_info "使用上级目录的配置: $cfg"
        fi
    else
        print_warning ".clang-format 配置文件不存在！"
        print_info "将使用默认的LLVM风格格式化代码"

        # 询问是否创建默认配置文件
        printf "是否创建默认的.clang-format配置文件？(y/N): "
        read REPLY
        case $REPLY in
            [Yy]|[Yy][Ee][Ss])
                create_default_config
                ;;
        esac
    fi
}

# 创建默认的.clang-format配置文件
create_default_config() {
    cat > .clang-format << 'EOF'
BasedOnStyle: LLVM

# 对齐连续的宏定义
AlignConsecutiveMacros: AcrossEmptyLinesAndComments

# 控制行宽，避免行太长
ColumnLimit: 120

# 控制语句的括号前有空格
SpaceBeforeParens: ControlStatements

# 赋值操作符前插入空格
SpaceBeforeAssignmentOperators: true

# 指针对齐方式，左对齐
PointerAlignment: Left

# 模板声明总是换行
AlwaysBreakTemplateDeclarations: true

# 使用4个空格缩进
IndentWidth: 4

# 使用4个空格表示一个制表符
TabWidth: 4

# 设置访问修饰符（public/private/protected）的缩进偏移为0
AccessModifierOffset: -4

# 控制每行最大空行数（避免空行过多）
MaxEmptyLinesToKeep: 1

# 启用注释对齐，注释前保留一个空格
AlignTrailingComments: true  

# 启用注释前空格
SpacesBeforeTrailingComments: 1

# 对齐转义的换行符到左侧
AlignEscapedNewlinesLeft: true

# 允许将参数换行，但保持多个参数在同一行（打包参数）
AllowAllParametersOfDeclarationOnNextLine: true

# 启用参数打包，减少换行次数
BinPackParameters: true

EOF
    print_success "已创建默认的 .clang-format 配置文件"
}

# 查找所有C/C++文件
find_source_files() {
    local search_dir="${1:-.}"
    print_info "正在搜索C/C++源文件... (路径: $search_dir)"

    # 创建临时文件来存储文件列表
    temp_file=$(mktemp)

    # 使用find命令查找所有C/C++文件，排除构建目录
    find "$search_dir" \( -name "*.c" -o -name "*.cpp" -o -name "*.cc" -o -name "*.cxx" -o -name "*.h" -o -name "*.hpp" \) \
         -not -path "./build/*" \
         -not -path "./.git/*" \
         -not -path "./output/*" \
         -not -path "./dist/*" \
         -not -path "./target/*" \
         -not -path "./cmake-build-*/*" \
         > "$temp_file"

    if [ ! -s "$temp_file" ]; then
        print_warning "未找到任何C/C++源文件"
        rm -f "$temp_file"
        return 1
    fi

    total_files=$(wc -l < "$temp_file")
    print_success "找到 $total_files 个C/C++源文件"

    # 将临时文件路径保存到全局变量
    SOURCE_FILES_TEMP="$temp_file"
    return 0
}

# 格式化单个文件
format_file() {
    local file="$1"
    local temp_file=$(mktemp)

    # 创建备份
    cp "$file" "$temp_file"

    # 尝试格式化
    if clang-format -i "$file" 2>/dev/null; then
        # 检查文件是否有变化
        if ! cmp -s "$file" "$temp_file"; then
            print_success "已格式化: $file"
            formatted_count=$((formatted_count + 1))
        else
            print_info "无需更改: $file"
        fi
    else
        print_error "格式化失败: $file"
        failed_count=$((failed_count + 1))
        # 恢复原文件
        cp "$temp_file" "$file"
    fi

    # 清理临时文件
    rm -f "$temp_file"
    return 0
}

# 显示帮助信息
show_help() {
    echo "C/C++代码格式化脚本"
    echo ""
    echo "用法: $0 [选项] [目录]"
    echo ""
    echo "选项:"
    echo "  -h, --help      显示此帮助信息"
    echo "  -v, --verbose   详细输出模式"
    echo "  -d, --dry-run   仅显示将要格式化的文件，不实际格式化"
    echo "  -c, --check     检查代码格式，返回是否有文件需要格式化"
    echo ""
    echo "示例:"
    echo "  $0              # 格式化当前目录下所有C/C++文件"
    echo "  $0 src/         # 格式化src目录下所有C/C++文件"
    echo "  $0 --dry-run    # 预览将要格式化的文件"
    echo "  $0 --check      # 检查代码格式"
}

# 主函数
main() {
    local verbose=false
    local dry_run=false
    local check_mode=false
    local target_dir="."

    # 解析命令行参数
    while [ $# -gt 0 ]; do
        case $1 in
            -h|--help)
                show_help
                exit 0
                ;;
            -v|--verbose)
                verbose=true
                shift
                ;;
            -d|--dry-run)
                dry_run=true
                shift
                ;;
            -c|--check)
                check_mode=true
                shift
                ;;
            -*)
                print_error "未知选项: $1"
                show_help
                exit 1
                ;;
            *)
                if [ -d "$1" ]; then
                    target_dir="$1"
                    shift
                else
                    print_error "目录不存在: $1"
                    show_help
                    exit 1
                fi
                ;;
        esac
    done

    print_info "开始C/C++代码格式化..."

    # 检查环境
    check_clang_format
    check_config_file

    # 查找源文件
    if ! find_source_files "$target_dir"; then
        exit 1
    fi

    # 初始化计数器
    formatted_count=0
    failed_count=0
    total_files=$(wc -l < "$SOURCE_FILES_TEMP")

    if [ "$dry_run" = "true" ]; then
        print_info "预览模式 - 以下文件将被格式化："
        cat "$SOURCE_FILES_TEMP"
        rm -f "$SOURCE_FILES_TEMP"
        exit 0
    fi

    if [ "$check_mode" = "true" ]; then
        print_info "检查代码格式..."
        needs_formatting=false

        while IFS= read -r file; do
            if ! clang-format --dry-run --Werror "$file" >/dev/null 2>&1; then
                echo "$file"
                needs_formatting=true
            fi
        done < "$SOURCE_FILES_TEMP"

        rm -f "$SOURCE_FILES_TEMP"

        if [ "$needs_formatting" = "true" ]; then
            print_warning "存在需要格式化的文件"
            exit 1
        else
            print_success "所有文件格式正确"
            exit 0
        fi
    fi

    # 格式化文件
    print_info "开始格式化文件..."
    current=0

    while IFS= read -r file; do
        current=$((current + 1))

        if [ "$verbose" = "true" ]; then
            echo -n "[$current/$total_files] "
        fi

        format_file "$file"
    done < "$SOURCE_FILES_TEMP"

    # 清理临时文件
    rm -f "$SOURCE_FILES_TEMP"

    # 显示结果
    echo ""
    print_success "格式化完成！"
    echo "总文件数: $total_files"
    echo "已格式化: $formatted_count"
    echo "失败文件: $failed_count"

    if [ $failed_count -gt 0 ]; then
        print_error "有 $failed_count 个文件格式化失败"
        exit 1
    fi
}

# 运行主函数
main "$@"
