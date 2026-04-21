include(GoogleTest)

# ── integration_tests (logger + publisher together) ──────────────────────────
add_executable(integration_tests
    integration/log_engine_pipeline_test.cpp
    integration/full_pipeline_test.cpp
)
target_include_directories(integration_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/integration
    ${CMAKE_SOURCE_DIR}/modules/logger/test
)
target_link_libraries(integration_tests
    PRIVATE
        logger::logger
        publisher::publisher
        common::common
        GTest::gtest_main
)
target_compile_options(integration_tests PRIVATE -Wall -Wextra -Wpedantic)
gtest_discover_tests(integration_tests)

# ── stress_tests (logger internals under load) ───────────────────────────────
add_executable(stress_tests
    stress/mpsc_queue_stress_test.cpp
    stress/freelist_stress_test.cpp
    stress/log_engine_stress_test.cpp
)
target_include_directories(stress_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/stress
    ${CMAKE_CURRENT_SOURCE_DIR}/harness
)
target_link_libraries(stress_tests
    PRIVATE
        logger::logger
        publisher::publisher
        common::common
        GTest::gtest_main
)
target_compile_options(stress_tests PRIVATE -Wall -Wextra -Wpedantic)
gtest_discover_tests(stress_tests)
