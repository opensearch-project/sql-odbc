project(performance_results)

# Source, headers, and include dirs
set(SOURCE_FILES performance_odbc_results.cpp)
include_directories(	${UT_HELPER}
						${IT_HELPER}
						${OPENSEARCHODBC_SRC}
						${VLD_SRC}  )

# Generate executable
add_executable(performance_results ${SOURCE_FILES})

# Find packages from vcpkg
find_package(GTest CONFIG REQUIRED)

# Library dependencies
target_link_libraries(performance_results sqlodbc itodbc_helper ut_helper GTest::gtest_main)
target_compile_definitions(performance_results PUBLIC _UNICODE UNICODE)
