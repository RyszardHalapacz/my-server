cmake_minimum_required(VERSION 3.14)
project(MyApp)

#set(CMAKE_CXX_COMPILER "clang++")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE "Release")

option(DEBUG_MODE "Enable debugging mode" OFF)
if (DEBUG_MODE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

include_directories("${CMAKE_SOURCE_DIR}/include")
include_directories("${CMAKE_SOURCE_DIR}/include/messages")
include_directories("${CMAKE_SOURCE_DIR}/include/server")
include_directories("${CMAKE_SOURCE_DIR}/include/handler")
include_directories("${CMAKE_SOURCE_DIR}/include/publisher")

include_directories("${CMAKE_SOURCE_DIR}/include/logger/registry")

include_directories("${CMAKE_SOURCE_DIR}/lib")
#GTEST

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

#GTEST
enable_testing()
#cmake -DDEBUG_MODE=ON ..
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")

set (MY_SOURCES
src/server/server.cpp
src/handler/ThreadedDatabaseHandler.cpp
src/handler/ConditionVariableDatabaseHandler.cpp
src/logger/log_names.cpp
#src/httpserver.cpp
)

set(TEST_SOURCES 
${MY_SOURCES}  
test/test.cpp
test/logger/log_names_test.cpp
test/logger/payloads/payload_base_test.cpp
test/logger/payloads/request_payload_test.cpp
test/logger/payloads/payload_register_test.cpp
test/logger/payloads/builder_test.cpp
test/logger/header_args_test.cpp
test/logger/logger_header_smoke_test.cpp
test/logger/logger_header_negative_test.cpp
test/logger/core/StreamAdapter_test.cpp
test/publisher/sink/JsonSink_test.cpp
test/publisher/policy/Policy_test.cpp
test/publisher/Publisher_test.cpp
test/logger/integration/full_pipeline_test.cpp
test/server/SingleThreadServer_test.cpp 
)

add_executable(
  main_test
  ${TEST_SOURCES}

)
target_link_libraries(main_test GTest::gtest_main
)


include(GoogleTest)
gtest_discover_tests(main_test)


add_executable(MyApp 
  ${MY_SOURCES}  
    src/main.cpp 
)
#target_link_libraries(MyApp -lpqxx -lpq)

