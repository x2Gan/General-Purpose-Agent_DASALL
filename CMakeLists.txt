cmake_minimum_required(VERSION 3.16)
project(DASALL_OS VERSION 0.1.0 LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(DASALLOptions)
include(DASALLThirdParty)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(DASALL_ENABLE_TESTS "Enable DASALL tests" ON)

message(STATUS "DASALL dependency priority: submodule > local cache > FetchContent")
message(STATUS "DASALL local dependency cache: ${DASALL_THIRD_PARTY_CACHE_DIR}")

add_subdirectory(contracts)
add_subdirectory(runtime)
add_subdirectory(cognition)
add_subdirectory(llm)
add_subdirectory(tools)
add_subdirectory(memory)
add_subdirectory(knowledge)
add_subdirectory(services)
add_subdirectory(multi_agent)
add_subdirectory(platform)
add_subdirectory(infra)
add_subdirectory(apps)

if(DASALL_ENABLE_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
