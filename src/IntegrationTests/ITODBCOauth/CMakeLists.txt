project(itodbc_Oauth)

# Source, headers, and include dirs
set(SOURCE_FILES test_odbc_Oauth.cpp)
include_directories(	${UT_HELPER}
						${OPENSEARCHODBC_SRC}
						${RAPIDJSON_SRC} 
						${RABBIT_SRC}		
						${LIBCURL_SRC}
						${VLD_SRC})

# Generate executable
add_executable(itodbc_Oauth ${SOURCE_FILES})

# Find packages from vcpkg
find_package(GTest CONFIG REQUIRED)

# Library dependencies
target_link_libraries(itodbc_Oauth ut_helper GTest::gtest_main aws-cpp-sdk-core ${VLD})
target_compile_definitions(itodbc_Oauth PUBLIC _UNICODE UNICODE)
