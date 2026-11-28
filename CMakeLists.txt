cmake_minimum_required(VERSION 3.18..3.22)
project(chen VERSION 1.6.0)

set(ROOT_DIR ${PROJECT_SOURCE_DIR})

configure_file(cmake/Version.hpp.in ${PROJECT_SOURCE_DIR}/src/Version.hpp @ONLY)

include(cmake/common.cmake)
include(cmake/utils.cmake)
include(cmake/dependencies.cmake)

# 收集源文件
chen_set_sources(${PROJECT_SOURCE_DIR}/src SOURCES Orm)

set(LIB_SRC
        ${SOURCES}
)

# 生成ragel文件
ragelmaker(src/Http/http11_parser.rl LIB_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/Http)
ragelmaker(src/Http/httpclient_parser.rl LIB_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/Http)
ragelmaker(src/Http/uri.rl LIB_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/Http)

# 生成protobuf文件
protobuf_maker(src/Rpc/rpc.proto LIB_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/Rpc)

add_library(chen SHARED ${LIB_SRC})

# 共享库版本化：SONAME 固定为 libchen.so.<major>，minor/patch 升级保持 ABI 兼容。
# 模块 .so 的 DT_NEEDED 记录的是 SONAME，所以升级 minor 版本库不必重编模块；
# 只有破坏对外 ABI 时才提升 project(VERSION) 的主版本号（同时需要重编模块）。
set_target_properties(chen PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR})

target_link_libraries(chen
        PUBLIC
        ${JSONCPP_LIB}
        ${TINYXML2_LIB}
        ${YAML_CPP_LIB}
        ${HIREDIS_VIP_LIB}
        ${PROTOBUF_LIB}
        ${SQLITE3_LIB}
        ${OPENSSL_LIBRARIES}
        ${EVENT_LIB}
        ${MYSQLCLIENT_LIB}
        ${PostgreSQL_LIBRARIES}
        Boost::context
        ${NGHTTP2_LIB}
        ${PTHREAD_LIB}
        PRIVATE
        ${CRYPT_LIB}
        ${DL_LIB}
)
force_redefine_file_macro_for_sources(chen)
chen_set_target_output_dir(chen ${PROJECT_SOURCE_DIR}/lib)

# 可执行文件只链 libchen，依赖由 chen 的 PUBLIC 接口传递
set(LIBS
        chen
)

if (CHEN_BUILD_TESTS)
    enable_testing()
endif ()

add_subdirectory(tests)

set(ORM_SRC
        src/Orm/Column.cc
        src/Orm/Index.cc
        src/Orm/Orm.cc
        src/Orm/Table.cc
        src/Orm/Util.cc
)
chen_add_executable(orm "${ORM_SRC}" "" "${LIBS}")

chen_add_executable(server "servers/Main.cc" chen "${LIBS}")

# 冒烟用例：验证 bin/server 能正常链接并响应 -v（不加载配置、不监听端口）
if (CHEN_BUILD_TESTS)
    # SONAME 冒烟：模块 .so 记录的是 SONAME，符号链接名一旦漂移，已编译模块将无法 dlopen
    add_test(NAME libchen_soname
            COMMAND ${CMAKE_COMMAND}
            -DSONAME_FILE=$<TARGET_SONAME_FILE:chen>
            -DEXPECTED_NAME=libchen.so.${PROJECT_VERSION_MAJOR}
            -P ${PROJECT_SOURCE_DIR}/tests/CheckSoname.cmake)
    set_tests_properties(libchen_soname PROPERTIES
            LABELS "smoke"
            TIMEOUT 30
            PASS_REGULAR_EXPRESSION "SONAME ok:")

    add_test(NAME server_version COMMAND server -v)
    set_tests_properties(server_version PROPERTIES
            LABELS "smoke"
            TIMEOUT 30
            PASS_REGULAR_EXPRESSION "chen/")
endif ()

chen_add_executable(converter "tools/scripts/XmlConverter.cc" chen "${LIBS}")
chen_set_target_output_dir(converter ${PROJECT_SOURCE_DIR}/tools/bin)

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
