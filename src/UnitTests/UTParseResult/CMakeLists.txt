project(ut_parse_result)

# Source, headers, and include dirs
set(SOURCE_FILES test_parse_result.cpp)
include_directories(	${OPENSEARCHODBC_SRC}
						${RAPIDJSON_SRC}
						${RABBIT_SRC}  )

# Generate executable
add_executable(ut_parse_result ${SOURCE_FILES})

# Find packages from vcpkg
find_package(GTest CONFIG REQUIRED)

# Library dependencies
target_link_libraries(ut_parse_result sqlodbc GTest::gtest_main)
target_compile_definitions(ut_parse_result PUBLIC _UNICODE UNICODE)
