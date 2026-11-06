# dependencies.cmake - 第三方库和依赖项配置
#
# 说明：所有依赖都解析为**明确的库路径**（deps/ 内的静态三方库 + 系统库），
# 并在 configure 阶段用 `<NAME> = <path>` 打印出来，便于确认到底用了哪个库。

set(DEPS_ROOT ${ROOT_DIR}/deps)

include_directories(.)
include_directories(${DEPS_ROOT}/include)
link_directories(${DEPS_ROOT}/lib)

set(BUILD_SHARED_LIBS OFF)
list(PREPEND CMAKE_PREFIX_PATH ${DEPS_ROOT})
list(PREPEND CMAKE_LIBRARY_PATH ${DEPS_ROOT}/lib)
list(PREPEND CMAKE_INCLUDE_PATH ${DEPS_ROOT}/include)

# ---------------------------------------------------------------------------
# 1) deps/ 内的三方静态库：强制只从 deps/lib 找 .a，并打印解析路径
# ---------------------------------------------------------------------------
set(_CHEN_LIB_SUFFIXES_DEFAULT ${CMAKE_FIND_LIBRARY_SUFFIXES})
set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")

find_library(JSONCPP_LIB jsoncpp PATHS ${DEPS_ROOT}/lib NO_DEFAULT_PATH)
find_library(TINYXML2_LIB tinyxml2 PATHS ${DEPS_ROOT}/lib NO_DEFAULT_PATH)
find_library(HIREDIS_VIP_LIB hiredis_vip PATHS ${DEPS_ROOT}/lib NO_DEFAULT_PATH)
find_library(YAML_CPP_LIB yaml-cpp PATHS ${DEPS_ROOT}/lib NO_DEFAULT_PATH)
find_library(PROTOBUF_LIB protobuf PATHS ${DEPS_ROOT}/lib NO_DEFAULT_PATH)

foreach (_dep JSONCPP_LIB TINYXML2_LIB HIREDIS_VIP_LIB YAML_CPP_LIB PROTOBUF_LIB)
    if (NOT ${_dep})
        message(FATAL_ERROR "${_dep} 未找到：${DEPS_ROOT}/lib（先执行 ./deps.py sync）")
    endif ()
    message(STATUS "${_dep} = ${${_dep}}")
endforeach ()

# protoc（生成 *.pb.cc/.h 用），同样来自 deps/bin
set(PROTOBUF_PROTOC_EXECUTABLE ${DEPS_ROOT}/bin/protoc)
set(Protobuf_PROTOC_EXECUTABLE ${PROTOBUF_PROTOC_EXECUTABLE})
if (NOT EXISTS ${PROTOBUF_PROTOC_EXECUTABLE})
    message(FATAL_ERROR "protoc 未找到：${PROTOBUF_PROTOC_EXECUTABLE}（先执行 ./deps.py sync）")
endif ()
message(STATUS "protoc = ${PROTOBUF_PROTOC_EXECUTABLE}")

# ---------------------------------------------------------------------------
# 2) 系统库：恢复平台默认后缀（.so/.a，mac 为 .dylib/.a），显式解析路径
# ---------------------------------------------------------------------------
set(CMAKE_FIND_LIBRARY_SUFFIXES ${_CHEN_LIB_SUFFIXES_DEFAULT})

set(_SYS_DEPS
        PTHREAD_LIB pthread
        DL_LIB dl
        CRYPT_LIB crypt
        SQLITE3_LIB sqlite3
        EVENT_LIB event
        MYSQLCLIENT_LIB mysqlclient
        NGHTTP2_LIB nghttp2
)
list(LENGTH _SYS_DEPS _sys_len)
math(EXPR _sys_last "${_sys_len} - 1")
foreach (_i RANGE 0 ${_sys_last} 2)
    math(EXPR _j "${_i} + 1")
    list(GET _SYS_DEPS ${_i} _name)
    list(GET _SYS_DEPS ${_j} _lib)
    find_library(${_name} ${_lib})
    if (${_name})
        message(STATUS "${_name} = ${${_name}}")
    else ()
        message(STATUS "${_name} = (not found, fallback to -l${_lib})")
        set(${_name} ${_lib})
    endif ()
endforeach ()

# OpenSSL（静态）
set(OPENSSL_USE_STATIC_LIBS TRUE)
find_package(OpenSSL REQUIRED)
if (OPENSSL_FOUND)
    include_directories(${OPENSSL_INCLUDE_DIR})
    message(STATUS "OPENSSL_SSL_LIBRARY = ${OPENSSL_SSL_LIBRARY}")
    message(STATUS "OPENSSL_CRYPTO_LIBRARY = ${OPENSSL_CRYPTO_LIBRARY}")
endif ()

# Boost.Context
find_package(Boost REQUIRED COMPONENTS context CONFIG)
if (Boost_FOUND)
    include_directories(${Boost_INCLUDE_DIRS})
    get_target_property(_boost_ctx Boost::context IMPORTED_LOCATION)
    if (NOT _boost_ctx)
        get_target_property(_boost_ctx Boost::context IMPORTED_LOCATION_RELEASE)
    endif ()
    message(STATUS "Boost::context = ${_boost_ctx}")
endif ()

# nghttp2 头文件（库路径已由 NGHTTP2_LIB 解析）
find_package(PkgConfig REQUIRED)
pkg_check_modules(NGHTTP2 REQUIRED libnghttp2)
if (NGHTTP2_FOUND)
    include_directories(${NGHTTP2_INCLUDE_DIRS})
    message(STATUS "NGHTTP2_INCLUDE_DIRS = ${NGHTTP2_INCLUDE_DIRS}")
endif ()
