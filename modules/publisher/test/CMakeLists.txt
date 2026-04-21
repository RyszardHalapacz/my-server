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

add_executable(publisher_tests
    publisher_test.cpp
    policy/policy_test.cpp
    sink/json_sink_test.cpp
    sink/text_sink_test.cpp
    runtime/publisher_runtime_test.cpp
)
target_include_directories(publisher_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(publisher_tests PRIVATE publisher::publisher GTest::gtest_main)
target_compile_options(publisher_tests PRIVATE -Wall -Wextra -Wpedantic)
gtest_discover_tests(publisher_tests)
