#!/bin/bash
# 同步框架文件到 SDK
# 用法: ./sync_sdk.sh [sdk_path]
# 默认: ../chen-sdk-1.3.1

SDK="${1:-../chen-sdk-1.3.1}"

if [ ! -d "$SDK" ]; then
    echo "SDK 目录不存在: $SDK"
    exit 1
fi

# 1. 头文件（只同步 SDK 中已有的，保持集合稳定）
echo "=== 同步头文件 ==="
find "$SDK/include/chen" -name "*.h" | while read dst; do
    src="chen/${dst#$SDK/include/chen/}"
    if [ -f "$src" ]; then
        if ! diff -q "$src" "$dst" > /dev/null 2>&1; then
            cp "$src" "$dst"
            echo "  ✓ ${src#chen/}"
        fi
    fi
done

# 2. 新增头文件（chen/ 有但 SDK 没有的）
find chen -name "*.h" | while read src; do
    dst="$SDK/include/$src"
    if [ ! -f "$dst" ]; then
        mkdir -p "$(dirname "$dst")"
        cp "$src" "$dst"
        echo "  + ${src#chen/}"
    fi
done

# 3. README
echo "=== 同步 README ==="
find chen -name "README.md" | while read src; do
    dst="$SDK/include/$src"
    if [ -f "$dst" ]; then
        if ! diff -q "$src" "$dst" > /dev/null 2>&1; then
            cp "$src" "$dst"
            echo "  ✓ ${src#chen/}"
        fi
    else
        mkdir -p "$(dirname "$dst")"
        cp "$src" "$dst"
        echo "  + ${src#chen/}"
    fi
done

# 5. 库文件
echo "=== 同步库文件 ==="
if [ -f "lib/libchen.so" ]; then
    if ! diff -q lib/libchen.so "$SDK/lib/libchen.so" > /dev/null 2>&1; then
        cp lib/libchen.so "$SDK/lib/libchen.so" && echo "  ✓ lib/libchen.so"
    fi
fi

# 6. 可执行文件
echo "=== 同步可执行文件 ==="
for bin in bin/orm bin/server; do
    if [ -f "$bin" ] && [ -f "$SDK/$bin" ]; then
        if ! diff -q "$bin" "$SDK/$bin" > /dev/null 2>&1; then
            cp "$bin" "$SDK/$bin" && echo "  ✓ $bin"
        fi
    elif [ -f "$bin" ]; then
        cp "$bin" "$SDK/$bin" && echo "  ✓ $bin"
    fi
done

# 7. tools 目录（converter 二进制和脚本）
echo "=== 同步 tools ==="
rsync -a --delete tools/ "$SDK/tools/" --exclude='.gitkeep' --exclude='*.cc' 2>/dev/null || {
    # fallback: cp-based sync
    for f in tools/bin/converter; do
        if [ -f "$f" ]; then
            mkdir -p "$SDK/$(dirname "$f")"
            cp "$f" "$SDK/$f" && echo "  ✓ $f"
        fi
    done
    for f in tools/scripts/*.{py,sh}; do
        if [ -f "$f" ]; then
            mkdir -p "$SDK/$(dirname "$f")"
            if [ ! -f "$SDK/$f" ] || ! diff -q "$f" "$SDK/$f" > /dev/null 2>&1; then
                cp "$f" "$SDK/$f" && echo "  ✓ $f"
            fi
        fi
    done
}

echo "=== 同步完成 ==="
