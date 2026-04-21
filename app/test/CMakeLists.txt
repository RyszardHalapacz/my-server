include(GoogleTest)

# ── server_tests (app-level — not cross-module) ──────────────────────────────
add_executable(server_tests
    server/metrics_mixin_test.cpp
    server/server_components_test.cpp
    ${CMAKE_SOURCE_DIR}/app/src/server_hooks.cpp
)
target_include_directories(server_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/app/include
)
target_link_libraries(server_tests PRIVATE GTest::gtest_main)
target_compile_options(server_tests PRIVATE -Wall -Wextra -Wpedantic)
gtest_discover_tests(server_tests)
