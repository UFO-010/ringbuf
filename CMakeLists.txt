cmake_minimum_required(VERSION 3.15.0)

project(ringbuf LANGUAGES C CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(${PROJECT_NAME}_compiler_flags INTERFACE)
target_compile_features(${PROJECT_NAME}_compiler_flags INTERFACE cxx_std_17)

add_library(ringbuf INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/include/ringbuf.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/include/storage.hpp"
)

target_include_directories(${PROJECT_NAME} INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/include"
)
