cmake_minimum_required(VERSION 3.20)
project(logger CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(LOGGER_BUILD_TESTS "Build logger tests" ${PROJECT_IS_TOP_LEVEL})

if(NOT TARGET common::common)
    find_package(common CONFIG REQUIRED)
endif()
if(NOT TARGET publisher::publisher)
    find_package(publisher CONFIG REQUIRED)
endif()

add_library(logger STATIC
    src/log_engine.cpp
)
add_library(logger::logger ALIAS logger)

target_include_directories(logger
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(logger
    PUBLIC
        common::common
        publisher::publisher
)
target_compile_features(logger PUBLIC cxx_std_20)
target_compile_options(logger PRIVATE -Wall -Wextra -Wpedantic)

# ── install / export ──────────────────────────────────────────────────────────
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS logger
    EXPORT loggerTargets
    ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT loggerTargets
    FILE loggerTargets.cmake
    NAMESPACE logger::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/logger
)

configure_package_config_file(
    cmake/loggerConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/loggerConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/logger
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/loggerConfig.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/logger
)

# ── tests ─────────────────────────────────────────────────────────────────────
if(LOGGER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
