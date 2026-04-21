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

add_executable(common_tests
    log_names_test.cpp
)
target_link_libraries(common_tests PRIVATE common::common GTest::gtest_main)
target_compile_options(common_tests PRIVATE -Wall -Wextra -Wpedantic)
gtest_discover_tests(common_tests)
