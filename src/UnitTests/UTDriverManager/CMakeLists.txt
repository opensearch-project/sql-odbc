project(ut_driver_manager)

# Talks to the driver only through the unixODBC driver manager, so it links
# libodbc and never sqlodbc: linking both would bind the driver's own calls
# to its exported entry points straight back to the driver, hiding exactly
# the DM interposition this test is for. The DM loads the driver from the
# absolute path it was built at.

# Prefer the system driver manager, as an application would use (CI installs
# unixodbc); fall back to vcpkg's where there is none.
# (libodbc.so.2: without unixodbc-dev there is no libodbc.so symlink)
find_library(UNIXODBC_DM_LIB NAMES odbc libodbc.so.2
	NO_CMAKE_PATH NO_CMAKE_ENVIRONMENT_PATH)
if(NOT UNIXODBC_DM_LIB)
	find_library(UNIXODBC_DM_LIB NAMES odbc REQUIRED)
endif()
message(STATUS "ut_driver_manager uses driver manager ${UNIXODBC_DM_LIB}")

add_executable(ut_driver_manager test_driver_manager.cpp)
add_dependencies(ut_driver_manager sqlodbc)
find_path(UNIXODBC_INCLUDE_DIR sqlext.h REQUIRED)
target_include_directories(ut_driver_manager PRIVATE ${UT_HELPER} ${UNIXODBC_INCLUDE_DIR})

find_package(GTest CONFIG REQUIRED)
target_link_libraries(ut_driver_manager ${UNIXODBC_DM_LIB} GTest::gtest_main)
# No UNICODE: the tests call the W functions explicitly
target_compile_definitions(ut_driver_manager PRIVATE
	DM_TEST_DRIVER="$<TARGET_FILE:sqlodbc>"
	DM_TEST_CURSOR_STUB="${CMAKE_CURRENT_SOURCE_DIR}/cursor_stub.py")
