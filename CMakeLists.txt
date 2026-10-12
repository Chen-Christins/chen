cmake_minimum_required(VERSION 3.18..3.22)
project(chen VERSION 1.5.5)

set(ROOT_DIR ${PROJECT_SOURCE_DIR})

configure_file(cmake/version.h.in ${PROJECT_SOURCE_DIR}/chen/version.h @ONLY)

include(cmake/common.cmake)
include(cmake/utils.cmake)
include(cmake/dependencies.cmake)

# 收集源文件
chen_set_sources(${PROJECT_SOURCE_DIR}/chen SOURCES orm)

set(LIB_SRC
    ${SOURCES}
)

# 生成ragel文件
ragelmaker(chen/http/http11_parser.rl LIB_SRC ${CMAKE_CURRENT_SOURCE_DIR}/chen/http)
ragelmaker(chen/http/httpclient_parser.rl LIB_SRC ${CMAKE_CURRENT_SOURCE_DIR}/chen/http)
ragelmaker(chen/http/uri.rl LIB_SRC ${CMAKE_CURRENT_SOURCE_DIR}/chen/http)

# 生成protobuf文件
protobuf_maker(chen/rpc/rpc.proto LIB_SRC ${CMAKE_CURRENT_SOURCE_DIR}/chen/rpc)

add_library(chen SHARED ${LIB_SRC})

# 依赖统一链接进 libchen.so，并区分可见性：
#  - PUBLIC：这些库的头文件出现在 chen 的公开头文件里（protobuf/yaml-cpp/tinyxml2/jsoncpp/
#    hiredis-vip/nghttp2/sqlite3/mysql/event/openssl/boost/pthread），消费者需要从而传递；
#  - PRIVATE：仅实现内部使用、公开头文件不暴露（crypt/dl）。
# 三方静态库静态进 libchen 后，其符号随共享库导出，下游 dlopen 的模块可直接解析。
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

add_subdirectory(tests)

set(ORM_SRC
    chen/orm/column.cc
    chen/orm/index.cc
    chen/orm/orm.cc
    chen/orm/table.cc
    chen/orm/util.cc
)
chen_add_executable(orm "${ORM_SRC}" "" "${LIBS}")

chen_add_executable(server "servers/main.cc" chen "${LIBS}")

chen_add_executable(converter "tools/scripts/xml_converter.cc" chen "${LIBS}")
chen_set_target_output_dir(converter ${PROJECT_SOURCE_DIR}/tools/bin)

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
