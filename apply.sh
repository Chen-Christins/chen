#!/bin/sh

# SDK 同步脚本
# 将 SDK 目录下的部分内容同步到上一级目录

set -e  # 遇到错误时退出

# 获取脚本所在目录 (兼容 sh 和 bash)
SDK_DIR="$(cd "$(dirname "$0")" && pwd)"
PARENT_DIR="$(dirname "$SDK_DIR")"

echo "SDK 目录: $SDK_DIR"
echo "目标目录: $PARENT_DIR"
echo ""

# 同步函数
sync_directory() {
    local src="$1"
    local dst="$2"
    local dir_name="$(basename "$src")"

    echo "正在同步: $dir_name"

    # 使用 rsync 同步内容
    # -a: 归档模式，保留权限、时间戳等
    # -v: 详细输出
    # --delete: 删除目标目录中源目录没有的文件（可选，注释掉以保留文件）
    # --include='*/' --include='*' --exclude='': 包含所有目录和文件（包括空目录）
    rsync -av --include='*/' --include='*' --exclude='' "$src"/ "$dst"/

    echo "  ✓ $dir_name 同步完成"
}

# 定义要同步的目录列表
# 每行一个目录名
sync_list() {
    echo "bin"
    echo "tools"
    echo "protocol"
    echo "resources"
}

# 执行同步
sync_list | while IFS= read -r dir; do
    src_path="$SDK_DIR/$dir"
    dst_path="$PARENT_DIR/$dir"

    if [ -d "$src_path" ]; then
        sync_directory "$src_path" "$dst_path"
    else
        echo "⚠️  警告: 源目录不存在 $src_path"
    fi
    echo ""
done

echo "所有同步操作完成！"