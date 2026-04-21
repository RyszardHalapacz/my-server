add_executable(MyApp
    src/main.cpp
    src/server_hooks.cpp
)

target_include_directories(MyApp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/lib
)

target_link_libraries(MyApp PRIVATE
    logger::logger
    publisher::publisher
    common::common
)

target_compile_options(MyApp PRIVATE -Wall -Wextra -Wpedantic)

if(MYSERVER_BUILD_TESTS)
    add_subdirectory(test)
endif()
