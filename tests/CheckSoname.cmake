# 校验 libchen 的 SONAME 符号链接名符合预期（libchen.so.<major>）。
#
# 用法：
#   cmake -DSONAME_FILE=<path> -DEXPECTED_NAME=<name> -P tests/CheckSoname.cmake
#
# 模块 .so 的 DT_NEEDED 记录的就是 SONAME；一旦 SONAME 意外变化（例如有人删掉
# CMakeLists 里的 SOVERSION），已编译的模块会在 dlopen 时报 "cannot open shared object"。

if (NOT DEFINED SONAME_FILE OR SONAME_FILE STREQUAL "")
    message(FATAL_ERROR "SONAME_FILE not provided")
endif ()
if (NOT DEFINED EXPECTED_NAME OR EXPECTED_NAME STREQUAL "")
    message(FATAL_ERROR "EXPECTED_NAME not provided")
endif ()

if (NOT EXISTS "${SONAME_FILE}")
    message(FATAL_ERROR "SONAME file not found: ${SONAME_FILE} "
            "(libchen 还没按新规则重新链接？先跑一次 `make`)")
endif ()

get_filename_component(soname "${SONAME_FILE}" NAME)
if (NOT soname STREQUAL "${EXPECTED_NAME}")
    message(FATAL_ERROR "unexpected SONAME link '${soname}', expected '${EXPECTED_NAME}'")
endif ()

message(STATUS "SONAME ok: ${soname} -> ${SONAME_FILE}")
