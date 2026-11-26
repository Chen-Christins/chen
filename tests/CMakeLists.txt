cmake_minimum_required(VERSION 3.22)
project(tests)

if (CHEN_BUILD_TESTS)
    # ------------------------------------------------------------------
    # 单元测试：tests/Unit/*.cpp，统一使用 tests/TestHarness.hpp
    # 运行方式：ctest --test-dir build -L unit --output-on-failure
    # 约定：不依赖网络、不依赖外部配置/数据库，单次运行 < 1s
    # ------------------------------------------------------------------
    function(chen_add_unit_test targetname source)
        chen_add_executable(${targetname} ${source} "" "${LIBS}")
        add_test(NAME ${targetname} COMMAND ${targetname})
        set_tests_properties(${targetname} PROPERTIES
                LABELS "unit"
                TIMEOUT 60
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    endfunction()

    chen_add_unit_test(test_unit_bytearray Unit/TestByteArray.cpp)
    chen_add_unit_test(test_unit_stringutil Unit/TestStringUtil.cpp)
    chen_add_unit_test(test_unit_config Unit/TestConfig.cpp)
    chen_add_unit_test(test_unit_timeutil Unit/TestTimeUtil.cpp)

    # ------------------------------------------------------------------
    # 手工/联调用的历史测试程序：默认不注册到 CTest（部分用例需要
    # 监听端口或真实数据库），保留 build 能力，产物在 tests/bin/ 手工执行。
    # ------------------------------------------------------------------
    # 注意：目标名不能叫 test —— enable_testing() 会生成同名 CMake 目标；
    # 这里用 test_basic，通过 OUTPUT_NAME 保持可执行文件仍叫 test。
    chen_add_executable(test_basic "Test.cc" "" "${LIBS}")
    set_target_properties(test_basic PROPERTIES OUTPUT_NAME test)
    chen_add_executable(test_config "TestConfig.cc" "" "${LIBS}")
    chen_add_executable(test_config_xml "TestConfigXml.cc" "" "${LIBS}")
    chen_add_executable(test_rate_limiter "TestRateLimiter.cc" "" "${LIBS}")
    chen_add_executable(test_metrics "TestMetrics.cc" "" "${LIBS}")
    chen_add_executable(test_thread "TestThread.cc" "" "${LIBS}")
    chen_add_executable(test_fiber "TestFiber.cc" "" "${LIBS}")
    chen_add_executable(test_fiber_bench "TestFiberBench.cc" "" "${LIBS}")
    chen_add_executable(test_schedule "TestSchedule.cc" "" "${LIBS}")
    chen_add_executable(test_massive_schedule "TestMassiveSchedule.cc" "" "${LIBS}")
    chen_add_executable(test_iomanager "TestIOManager.cc" "" "${LIBS}")
    chen_add_executable(test_hook "TestHook.cc" "" "${LIBS}")
    chen_add_executable(test_address "TestAddress.cc" "" "${LIBS}")
    chen_add_executable(test_socket "TestSocket.cc" "" "${LIBS}")
    chen_add_executable(test_bytearray "TestByteArray.cc" "" "${LIBS}")
    chen_add_executable(test_tcp_server "TestTcpServer.cc" "" "${LIBS}")
    chen_add_executable(test_http "TestHttp.cc" "" "${LIBS}")
    chen_add_executable(test_http_parser "TestHttpParser.cc" "" "${LIBS}")
    chen_add_executable(test_http_server "TestHttpServer.cc" "" "${LIBS}")
    chen_add_executable(test_uri "TestUri.cc" "" "${LIBS}")
    chen_add_executable(test_http_connection "TestHttpConnection.cc" "" "${LIBS}")
    chen_add_executable(test_daemon "TestDaemon.cc" "" "${LIBS}")
    chen_add_executable(test_env "TestEnv.cc" "" "${LIBS}")
    chen_add_executable(test_application "TestApplication.cc" "" "${LIBS}")
    chen_add_executable(test_file_watcher "TestFileWatcher.cc" "" "${LIBS}")
    chen_add_executable(test_ws_client "TestWSClient.cc" "" "${LIBS}")
    chen_add_executable(test_ws_server "TestWSServer.cc" "" "${LIBS}")
    chen_add_executable(test_sse_server "TestSSEServer.cc" "" "${LIBS}")
    chen_add_executable(sse_push_example "SsePushExample.cc" "" "${LIBS}")
    chen_add_executable(test_rpc_server "TestRpcServer.cc" "" "${LIBS}")
    chen_add_executable(test_rpc_client "TestRpcClient.cc" "" "${LIBS}")
    chen_add_executable(test_rpc_hub "TestRpcHub.cc" "" "${LIBS}")
    chen_add_executable(test_game_rpc "TestGameRpc.cc" "" "${LIBS}")
    chen_add_executable(test_data_reader "TestDataReader.cc" "" "${LIBS}")
    chen_add_executable(test_game_client "TestGameClient.cc" "" "${LIBS}")
    chen_add_executable(test_rock "TestRock.cc" "" "${LIBS}")
    chen_add_executable(test_mysql "TestMySQL.cc" "" "${LIBS}")
    chen_add_executable(test_postgresql "TestPostgreSQL.cc" "" "${LIBS}")
    # ORM 生成器不在 libchen 中，单独编译 orm 源码
    set(TEST_ORM_GEN_SRC
            "TestOrmGen.cc"
            "${ROOT_DIR}/src/Orm/Column.cc"
            "${ROOT_DIR}/src/Orm/Index.cc"
            "${ROOT_DIR}/src/Orm/Table.cc"
            "${ROOT_DIR}/src/Orm/Util.cc")
    chen_add_executable(test_orm_gen "${TEST_ORM_GEN_SRC}" "" "${LIBS}")
    chen_add_executable(test_h2_server "TestH2Server.cc" "" "${LIBS}")
    chen_add_executable(test_time_util "TestTimeUtil.cc" "" "${LIBS}")
    chen_add_executable(test_encryptor_util "TestEncryptorUtil.cc" "" "${LIBS}")
    chen_add_executable(test_string_util "TestStringUtil.cc" "" "${LIBS}")
    chen_add_executable(test_future "TestFuture.cc" "" "${LIBS}")
    chen_add_executable(test_http_client "TestHttpClient.cc" "" "${LIBS}")
    chen_add_executable(test_log_size_rotate "TestLogSizeRotate.cc" "" "${LIBS}")
    chen_add_executable(test_log_reload_formatter "TestLogReloadFormatter.cc" "" "${LIBS}")
endif ()

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)

# Metrics 性能基准：不受 TEST 开关限制，建议用 Release 构建运行
chen_add_executable(test_metrics_bench "TestMetricsBench.cc" "" "${LIBS}")

# 游戏测试模块
add_library(game_module SHARED
        Game/GameModule.cc
        Game/GameRpcService.cc
)
force_redefine_file_macro_for_sources(game_module)
target_link_libraries(game_module ${LIBS})
chen_set_target_output_dir(game_module ${ROOT_DIR}/bin/module)

# 生命周期测试模块 — 验证 onLoad/onServerReady/onServerUp/onDeactivate/onUnload 流程
add_library(lifecycle_module SHARED
        ModuleTest/LifecycleModule.cc
)
force_redefine_file_macro_for_sources(lifecycle_module)
target_link_libraries(lifecycle_module ${LIBS})
chen_set_target_output_dir(lifecycle_module ${ROOT_DIR}/bin/module)