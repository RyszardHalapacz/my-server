cmake_minimum_required(VERSION 3.20)
project(MyServer CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(MYSERVER_BUILD_TESTS "Build all tests" ON)

# ── central GTest (reused by all modules & cross-module tests) ───────────────
if(MYSERVER_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    enable_testing()
endif()

# ── per-module test toggles (follow top-level) ───────────────────────────────
set(COMMON_BUILD_TESTS    ${MYSERVER_BUILD_TESTS} CACHE BOOL "" FORCE)
set(PUBLISHER_BUILD_TESTS ${MYSERVER_BUILD_TESTS} CACHE BOOL "" FORCE)
set(LOGGER_BUILD_TESTS    ${MYSERVER_BUILD_TESTS} CACHE BOOL "" FORCE)

# ── modules ──────────────────────────────────────────────────────────────────
add_subdirectory(modules/common)
add_subdirectory(modules/publisher)
add_subdirectory(modules/logger)

# ── application ──────────────────────────────────────────────────────────────
add_subdirectory(app)

# ── cross-module tests ───────────────────────────────────────────────────────
if(MYSERVER_BUILD_TESTS)
    add_subdirectory(test)
endif()
