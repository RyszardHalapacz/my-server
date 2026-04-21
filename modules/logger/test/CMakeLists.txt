include(FetchContent)
if(NOT TARGET GTest::gtest_main)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()
include(GoogleTest)

add_executable(logger_tests
    header_args_test.cpp
    logger_header_smoke_test.cpp
    logger_header_negative_test.cpp
    payloads/payload_base_test.cpp
    payloads/request_payload_test.cpp
    payloads/payload_register_test.cpp
    payloads/builder_test.cpp
    core/stream_adapter_test.cpp
    core/log_record_test.cpp
    core/freelist_test.cpp
    core/mpsc_queue_test.cpp
)
target_include_directories(logger_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(logger_tests PRIVATE logger::logger GTest::gtest_main)
target_compile_options(logger_tests PRIVATE -Wall -Wextra -Wpedantic)
gtest_discover_tests(logger_tests)
