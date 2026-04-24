cmake_minimum_required(VERSION 3.18)
project(cpp-httplib LANGUAGES CXX)

add_library(cpp-httplib INTERFACE)
add_library(httplib::httplib ALIAS cpp-httplib)

target_include_directories(cpp-httplib INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

# cpp-httplib v0.15.3, MIT License
# https://github.com/yhirose/cpp-httplib
