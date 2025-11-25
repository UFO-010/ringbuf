CMAKE_MINIMUM_REQUIRED(VERSION 3.15.0)
enable_testing()

set(CMAKE_INCLUDE_CURRENT_DIR ON)

project(spsc_test LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


set(SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/ringbuf_test.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ringbuf_thread_safe_test.cpp"
)

add_subdirectory(../ ringbuf)

add_executable(${PROJECT_NAME} ${SOURCES})

find_library(ringbuf ringbuf PATH_SUFFIXES ringbuf)

include(FetchContent)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        release-1.11.0
)

FetchContent_MakeAvailable(googletest)

target_link_libraries(${PROJECT_NAME} PRIVATE
    gtest_main
    gmock_main
    ringbuf
)

include(GoogleTest)
gtest_discover_tests(${PROJECT_NAME}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    DISCOVERY_TIMEOUT 30
)

add_test(NAME Test COMMAND ${PROJECT_NAME})
