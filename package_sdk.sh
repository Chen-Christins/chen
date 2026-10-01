#!/bin/sh

set -e

# 配置变量
PROJECT_NAME="chen"
SDK_NAME="${PROJECT_NAME}-sdk"
VERSION="1.0.0"
BUILD_DIR="build"
LIB_DIR="lib"
BIN_DIR="bin"
INCLUDE_DIR="include"
PROTOCOL_DIR="protocol"
RESOURCES_DIR="resources"
TOOLS_DIR="tools"

# 参数解析：支持 -v/--version 或第一个位置参数作为版本号
usage() {
    echo "用法: $0 [-v|--version <version>]" >&2
    exit 1
}

VERSION_FROM_CLI=""
while [ $# -gt 0 ]; do
    case "$1" in
        -v|--version)
            [ -n "$2" ] || usage
            VERSION_FROM_CLI="$2"
            shift 2
            ;;
        --version=*)
            VERSION_FROM_CLI="${1#*=}"
            shift
            ;;
        -v*)
            VERSION_FROM_CLI="${1#-v}"
            shift
            ;;
        -h|--help)
            usage
            ;;
        -* )
            echo "未知选项: $1" >&2
            usage
            ;;
        *)
            # 首个非选项参数，按版本号处理
            if [ -z "$VERSION_FROM_CLI" ]; then
                VERSION_FROM_CLI="$1"
            fi
            # 只处理第一个非选项参数，后续参数保留传递
            shift
            break
            ;;
    esac
done

if [ -n "$VERSION_FROM_CLI" ]; then
    VERSION="$VERSION_FROM_CLI"
fi

# 根据最终版本号更新输出目录
DIST_DIR="${SDK_NAME}-${VERSION}"

echo "开始打包 ${PROJECT_NAME} SDK..."

# 清理之前的构建
if [ -d "${BUILD_DIR}" ]; then
    echo "清理构建目录..."
    rm -rf "${BUILD_DIR}"
fi

if [ -d "${DIST_DIR}" ]; then
    echo "清理分发目录..."
    rm -rf "${DIST_DIR}"
fi

# 创建目录结构
echo "创建目录结构..."
mkdir -p "${DIST_DIR}/${INCLUDE_DIR}"
mkdir -p "${DIST_DIR}/${LIB_DIR}"
mkdir -p "${DIST_DIR}/${BIN_DIR}"
mkdir -p "${DIST_DIR}/${PROTOCOL_DIR}"
mkdir -p "${DIST_DIR}/${TOOLS_DIR}/bin"
mkdir -p "${DIST_DIR}/${TOOLS_DIR}/scripts"
mkdir -p "${DIST_DIR}/${TOOLS_DIR}/xlsx"
mkdir -p "${BUILD_DIR}"

# 构建项目
echo "构建项目..."
cd "${BUILD_DIR}"
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
cd ..

# 复制资源文件
echo "复制资源文件..."
cp apply.sh "${DIST_DIR}/"
cp -r "${PROTOCOL_DIR}"/* "${DIST_DIR}/${PROTOCOL_DIR}/"
cp "${TOOLS_DIR}/scripts"/*.py "${DIST_DIR}/${TOOLS_DIR}/scripts/"
cp "${TOOLS_DIR}/scripts"/*.sh "${DIST_DIR}/${TOOLS_DIR}/scripts/"
cp "${TOOLS_DIR}/bin"/* "${DIST_DIR}/${TOOLS_DIR}/bin/"

# 清理协议目录中的不必要文件
echo "清理协议目录中的不必要文件..."
rm -rf "${DIST_DIR}/${PROTOCOL_DIR}/include"/*
rm -rf "${DIST_DIR}/${PROTOCOL_DIR}/xml"/*
rm -rf "${DIST_DIR}/${PROTOCOL_DIR}"/README.md

# 清理工具目录中的不必要文件
echo "清理工具目录中的不必要文件..."


# 这里的资源目录和协议目录相同，都是 xml -> header 文件转换相关
cp -r "${DIST_DIR}/${PROTOCOL_DIR}" "${DIST_DIR}/${RESOURCES_DIR}" 

# 复制头文件（保持目录结构）
echo "复制头文件..."
find chen -name "*.h" -o -name "*.hh" -o -name "*.hpp" | while read file; do
    # 获取相对路径
    rel_path=$(dirname "$file")
    # 创建目标目录
    mkdir -p "${DIST_DIR}/${INCLUDE_DIR}/$rel_path"
    # 复制文件
    cp "$file" "${DIST_DIR}/${INCLUDE_DIR}/$rel_path/"
    echo "复制: $file"
done

# 复制各模块 README.md（保留目录结构，开发者用）
echo "复制模块文档..."
find chen -name "README.md" | while read file; do
    rel_path=$(dirname "$file")
    mkdir -p "${DIST_DIR}/${INCLUDE_DIR}/$rel_path"
    cp "$file" "${DIST_DIR}/${INCLUDE_DIR}/$rel_path/"
    echo "复制文档: $file"
done

# 复制动态库文件
echo "复制动态库文件..."
if [ -d "${BUILD_DIR}/lib" ]; then
    find "${BUILD_DIR}/lib" -name "*.so" -o -name "*.so.*" | while read lib_file; do
        cp "$lib_file" "${DIST_DIR}/${LIB_DIR}/"
        echo "复制库: $(basename "$lib_file")"
    done
else
    echo "警告: 未找到 lib 目录，尝试在 ./lib 目录中查找..."
    find "./lib" -name "*.so" -o -name "*.so.*" | while read lib_file; do
        cp "$lib_file" "${DIST_DIR}/${LIB_DIR}/"
        echo "复制库: $(basename "$lib_file")"
    done
fi

# 复制静态库文件（如果有）
echo "复制静态库文件..."
find "${BUILD_DIR}" -name "*.a" | while read lib_file; do
    cp "$lib_file" "${DIST_DIR}/${LIB_DIR}/"
    echo "复制静态库: $(basename "$lib_file")"
done

# 复制可执行程序
echo "复制可执行文件..."
find "./bin" -type f \( -name "orm" -o -name "server" \) | while read exec_file; do
    cp "$exec_file" "${DIST_DIR}/${BIN_DIR}/"
    echo "复制可执行文件: $(basename "$exec_file")"
done

# 复制模板文件
echo "复制模板文件..."
if [ -d "./template" ]; then
    cp -r "./template" "${DIST_DIR}/"template
    echo "复制模板目录"
else
    echo "警告: 未找到 template 目录，跳过复制模板文件。"
fi

# 创建 CMake 配置文件
echo "创建 CMake 配置文件..."
cat > "${DIST_DIR}/CMakeLists.txt" << 'EOF'
cmake_minimum_required(VERSION 3.22)
project(chen_sdk)

# 设置包含目录
set(CHEN_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${CHEN_INCLUDE_DIRS})

# 查找所有库文件
file(GLOB CHEN_LIBRARIES "${CMAKE_CURRENT_SOURCE_DIR}/lib/*.so" "${CMAKE_CURRENT_SOURCE_DIR}/lib/*.a")

# 创建导入目标
foreach(lib_file ${CHEN_LIBRARIES})
    get_filename_component(lib_name ${lib_file} NAME_WE)
    string(REGEX REPLACE "^lib" "" target_name ${lib_name})
    
    if(${lib_file} MATCHES "\\.so")
        add_library(${target_name} SHARED IMPORTED)
    else()
        add_library(${target_name} STATIC IMPORTED)
    endif()
    
    set_target_properties(${target_name} PROPERTIES
        IMPORTED_LOCATION ${lib_file}
        INTERFACE_INCLUDE_DIRECTORIES ${CHEN_INCLUDE_DIRS}
    )
    
    message(STATUS "Found library: ${target_name} -> ${lib_file}")
endforeach()

# 提供查找包的功能
set(CHEN_FOUND TRUE)
set(CHEN_INCLUDE_DIR ${CHEN_INCLUDE_DIRS})
set(CHEN_LIBRARY ${CHEN_LIBRARIES} PARENT_SCOPE)

# 查找并导入常用依赖库
find_library(PTHREAD pthread REQUIRED)
find_library(JSONCPP jsoncpp REQUIRED)
find_library(TINYXML2 tinyxml2 REQUIRED)
find_library(SQLITE3 sqlite3 REQUIRED)
find_library(EVENT event REQUIRED)
find_library(HIREDIS_VIP hiredis_vip REQUIRED)
find_library(MySQLClient mysqlclient REQUIRED)
find_library(YAML_CPP yaml-cpp REQUIRED)
find_library(Protobuf_LIBRARIES protobuf REQUIRED)
find_library(nghttp2 nghttp2 REQUIRED)

find_package(OpenSSL REQUIRED)
# find_package(yaml-cpp REQUIRED)
find_package(Protobuf REQUIRED)
find_package(Boost REQUIRED COMPONENTS context)

set(CHEN_SDK_DEP_LIBS
    ${PTHREAD}
    ${JSONCPP}
    ${TINYXML2}
    ${SQLITE3}
    # ${YAML_CPP_LIBRARIES}
    /usr/local/lib/libyaml-cpp.so
    ${OPENSSL_LIBRARIES}
    ${EVENT}
    ${HIREDIS_VIP}
    ${Protobuf_LIBRARIES}
    ${MySQLClient}
    ${nghttp2}
)

# Append the resolved Boost libraries (paths or linker names).
# Prefer to export the actual library file path for Boost::context when
# the imported target exists, so consumers don't need to call
# `find_package(Boost)` to resolve the imported target.
if(TARGET Boost::context)
    get_target_property(_boost_ctx_loc Boost::context IMPORTED_LOCATION_RELEASE)
    if(NOT _boost_ctx_loc)
        get_target_property(_boost_ctx_loc Boost::context IMPORTED_LOCATION)
    endif()
    if(_boost_ctx_loc)
        list(APPEND CHEN_SDK_DEP_LIBS ${_boost_ctx_loc})
    else()
        # Fallback to whatever Boost provided (may be a target name or linker flag)
        list(APPEND CHEN_SDK_DEP_LIBS ${Boost_LIBRARIES})
    endif()
else()
    list(APPEND CHEN_SDK_DEP_LIBS ${Boost_LIBRARIES})
endif()

# Expose Boost include directories to consumers of this SDK
set(CHEN_SDK_DEP_INCLUDE_DIRS ${Boost_INCLUDE_DIRS} PARENT_SCOPE)

set(CHEN_SDK_DEP_LIBS ${CHEN_SDK_DEP_LIBS} PARENT_SCOPE)

EOF

# 创建使用说明
echo "创建使用说明..."
cat > "${DIST_DIR}/README.md" << 'EOF'
# Chen SDK

这是一个打包的 Chen 框架 SDK。

## 目录结构

EOF

# 打包压缩
echo "创建压缩包..."

# 清理已存在的压缩包
rm -f "${DIST_DIR}.tar.gz" "${DIST_DIR}.zip"

# 生成 tar.gz 包（通用 Linux 发行）
# tar -czf "${DIST_DIR}.tar.gz" "${DIST_DIR}"
# echo "已生成: ${DIST_DIR}.tar.gz"

# 如果系统有 zip，则额外生成 zip 包（跨平台友好）
if command -v zip >/dev/null 2>&1; then
    zip -rq "${DIST_DIR}.zip" "${DIST_DIR}"
    echo "已生成: ${DIST_DIR}.zip"
fi

# 可选：生成 SHA256 校验文件（若有 sha256sum）
# if command -v sha256sum >/dev/null 2>&1; then
#     sha256sum "${DIST_DIR}.tar.gz" > "${DIST_DIR}.tar.gz.sha256"
#     [ -f "${DIST_DIR}.zip" ] && sha256sum "${DIST_DIR}.zip" > "${DIST_DIR}.zip.sha256"
#     echo "已生成校验文件: ${DIST_DIR}.tar.gz.sha256" \
#          $( [ -f "${DIST_DIR}.zip.sha256" ] && echo "${DIST_DIR}.zip.sha256" )
# fi

rm -rf "${DIST_DIR}"

# 删除构建目录
echo "清理构建目录..."
# make clean

echo "SDK 打包完成。输出位于: ${DIST_DIR}/ 以及压缩包 *.tar.gz/*.zip"
