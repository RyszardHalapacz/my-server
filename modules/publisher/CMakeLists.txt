cmake_minimum_required(VERSION 3.20)
project(publisher CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(PUBLISHER_BUILD_TESTS "Build publisher tests" ${PROJECT_IS_TOP_LEVEL})

if(NOT TARGET common::common)
    find_package(common CONFIG REQUIRED)
endif()

add_library(publisher STATIC
    src/sink_publisher.cpp
)
add_library(publisher::publisher ALIAS publisher)

target_include_directories(publisher
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(publisher PUBLIC common::common)
target_compile_features(publisher PUBLIC cxx_std_20)
target_compile_options(publisher PRIVATE -Wall -Wextra -Wpedantic)

# ── install / export ──────────────────────────────────────────────────────────
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS publisher
    EXPORT publisherTargets
    ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT publisherTargets
    FILE publisherTargets.cmake
    NAMESPACE publisher::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/publisher
)

configure_package_config_file(
    cmake/publisherConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/publisherConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/publisher
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/publisherConfig.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/publisher
)

# ── tests ─────────────────────────────────────────────────────────────────────
if(PUBLISHER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
