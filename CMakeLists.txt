cmake_minimum_required(VERSION 3.20)
project(MyServer CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ── Shared production library ─────────────────────────────────────────────────
add_library(myserver_lib STATIC
    src/logger/log_names.cpp
)

target_include_directories(myserver_lib PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/server
    ${CMAKE_SOURCE_DIR}/include/publisher
    ${CMAKE_SOURCE_DIR}/include/logger/registry
)

target_compile_options(myserver_lib PRIVATE
    -Wall -Wextra -Wpedantic
)

# ── Main application ──────────────────────────────────────────────────────────
add_executable(MyApp src/main.cpp)
target_link_libraries(MyApp PRIVATE myserver_lib)

# ── Tests ─────────────────────────────────────────────────────────────────────
enable_testing()
add_subdirectory(test)
