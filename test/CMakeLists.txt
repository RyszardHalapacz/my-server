include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(main_test
    logger/log_names_test.cpp
    logger/payloads/payload_base_test.cpp
    logger/payloads/request_payload_test.cpp
    logger/payloads/payload_register_test.cpp
    logger/payloads/builder_test.cpp
    logger/header_args_test.cpp
    logger/logger_header_smoke_test.cpp
    logger/logger_header_negative_test.cpp
    logger/core/stream_adapter_test.cpp
    logger/core/log_record_test.cpp
    logger/core/freelist_test.cpp
    logger/core/mpsc_queue_test.cpp
    logger/core/log_engine_pipeline_test.cpp
    publisher/sink/json_sink_test.cpp
    publisher/sink/text_sink_test.cpp
    publisher/policy/policy_test.cpp
    publisher/publisher_test.cpp
    logger/integration/full_pipeline_test.cpp
    server/single_thread_server_test.cpp
    server/single_thread_server_extended_test.cpp
    server/metrics_mixin_test.cpp
    stress/mpsc_queue_stress_test.cpp
    stress/freelist_stress_test.cpp
    stress/log_engine_stress_test.cpp
)

target_include_directories(main_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(main_test PRIVATE
    myserver_lib
    GTest::gtest_main
)

target_compile_options(main_test PRIVATE
    -Wall -Wextra -Wpedantic
)

include(GoogleTest)
gtest_discover_tests(main_test)
