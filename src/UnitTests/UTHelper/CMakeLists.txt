project(ut_helper)

# Source, headers, and include dirs
set(SOURCE_FILES unit_test_helper.cpp)	
set(HEADER_FILES unit_test_helper.h)
include_directories(${OPENSEARCHODBC_SRC})

# Generate dll (SHARED)
add_library(ut_helper SHARED ${SOURCE_FILES} ${HEADER_FILES})

# Find packages from vcpkg
find_package(GTest CONFIG REQUIRED)

# Library dependencies
target_link_libraries(ut_helper sqlodbc GTest::gtest_main)
target_compile_definitions(ut_helper PUBLIC _UNICODE UNICODE)
