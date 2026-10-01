# dependencies.cmake - 第三方库和依赖项配置

set(DEPS_ROOT ${ROOT_DIR}/deps)

include_directories(.)
include_directories(${DEPS_ROOT}/include)
link_directories(${DEPS_ROOT}/lib)

# 统一使用静态库
set(BUILD_SHARED_LIBS OFF)
set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")

# 统一将 find_package/find_library 的搜索前缀指向 deps 目录
list(PREPEND CMAKE_PREFIX_PATH ${DEPS_ROOT})
list(PREPEND CMAKE_LIBRARY_PATH ${DEPS_ROOT}/lib)
list(PREPEND CMAKE_INCLUDE_PATH ${DEPS_ROOT}/include)
# link_directories(/usr/local/lib)

# protobuf 文件将在后面添加 
set(Protobuf_ROOT ${DEPS_ROOT})
set(Protobuf_USE_STATIC_LIBS ON)
find_package(Protobuf REQUIRED MODULE)
if(PROTOBUF_FOUND)
    include_directories(${PROTOBUF_INCLUDE_DIRS})
endif()

# 导入库
find_library(PTHREAD pthread PATHS ${DEPS_ROOT}/lib)
find_library(JSONCPP jsoncpp PATHS ${DEPS_ROOT}/lib)

set(OPENSSL_USE_STATIC_LIBS TRUE)
find_package(OpenSSL REQUIRED)
if(OPENSSL_FOUND)
    include_directories(${OPENSSL_INCLUDE_DIR})
endif()

find_package(yaml-cpp REQUIRED
    PATHS
        ${DEPS_ROOT}
        ${DEPS_ROOT}/yaml-cpp-yaml-cpp-0.9.0
)
if(YAML_CPP_FOUND)
    include_directories(${YAML_CPP_INCLUDE_DIRS})
endif()

find_package(tinyxml2 REQUIRED
    PATHS
        ${DEPS_ROOT}
        ${DEPS_ROOT}/tinyxml2
)
if(TINYXML2_FOUND)
    include_directories(${TINYXML2_INCLUDE_DIRS})
endif()

find_package(Boost REQUIRED COMPONENTS context CONFIG)
if(Boost_FOUND)
    include_directories(${Boost_INCLUDE_DIRS})
    link_directories(${Boost_LIBRARY_DIRS})
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(NGHTTP2 REQUIRED libnghttp2)
if(NGHTTP2_FOUND)
    include_directories(${NGHTTP2_INCLUDE_DIRS})
    link_directories(${NGHTTP2_LIBRARY_DIRS})
endif()
