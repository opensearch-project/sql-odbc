project(ut_mylog)

# Source, headers, and include dirs
set(SOURCE_FILES test_mylog.cpp)
include_directories(${OPENSEARCHODBC_SRC})

# Generate executable
add_executable(ut_mylog ${SOURCE_FILES})

# Find packages from vcpkg
find_package(GTest CONFIG REQUIRED)

# Library dependencies
target_link_libraries(ut_mylog sqlodbc GTest::gtest_main)
target_compile_definitions(ut_mylog PUBLIC _UNICODE UNICODE)
