project(ut_convert)

# Source, headers, and include dirs
set(SOURCE_FILES test_convert.cpp)
include_directories(${OPENSEARCHODBC_SRC})

# Generate executable
add_executable(ut_convert ${SOURCE_FILES})

# Find packages from vcpkg
find_package(GTest CONFIG REQUIRED)

# Library dependencies
target_link_libraries(ut_convert sqlodbc GTest::gtest_main)
target_compile_definitions(ut_convert PUBLIC _UNICODE UNICODE)
