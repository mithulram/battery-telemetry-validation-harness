cmake_minimum_required(VERSION 3.16)
project(battery_telemetry_validation_harness LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_library(bms_validator
    src/validator.cpp
)
target_include_directories(bms_validator PUBLIC include)

add_executable(bms-validate src/main.cpp)
target_link_libraries(bms-validate PRIVATE bms_validator)

enable_testing()

add_executable(test_validator tests/test_validator.cpp)
target_link_libraries(test_validator PRIVATE bms_validator GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(test_validator)
