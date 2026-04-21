cmake_minimum_required(VERSION 3.20)
project(common CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(COMMON_BUILD_TESTS "Build common tests" ${PROJECT_IS_TOP_LEVEL})

add_library(common STATIC
    src/log_names.cpp
)
add_library(common::common ALIAS common)

target_include_directories(common
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(common PUBLIC cxx_std_20)
target_compile_options(common PRIVATE -Wall -Wextra -Wpedantic)

# ── install / export ──────────────────────────────────────────────────────────
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS common
    EXPORT commonTargets
    ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT commonTargets
    FILE commonTargets.cmake
    NAMESPACE common::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/common
)

configure_package_config_file(
    cmake/commonConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/commonConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/common
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/commonConfig.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/common
)

# ── tests ─────────────────────────────────────────────────────────────────────
if(COMMON_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
