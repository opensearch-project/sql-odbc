# Pre 3.16 versions of Windows set MD/MT incorrectly and cause linker 'warnings' which are actually serious issues
if(WIN32)
    cmake_minimum_required(VERSION 3.16)
else()
    cmake_minimum_required(VERSION 3.13)
endif()

project(global_make_list)

include("${CMAKE_CURRENT_SOURCE_DIR}/modules/code-coverage.cmake")
add_code_coverage_all_targets(EXCLUDE libraries aws-cpp-sdk googletest IntegrationTests)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
	set(BITNESS 64)
	set(BITNESS_NAME "x64")
	set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../build/odbc/lib")
	set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../build/odbc/lib")
	set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../build/odbc/bin")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
	set(BITNESS 32)
	set(BITNESS_NAME "x86")
	set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../build/odbc/lib")
	set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../build/odbc/lib")
	set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../build/odbc/bin")
endif()

# This wasn't being picked up on mac, causes some symbol errors
if(APPLE)
	set(CMAKE_CXX_STANDARD 17)
	add_definitions(-DHAVE_STRLCAT)
	# Do not use shared libraries for AWS SDK.
	option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
	# Set BUILD_WITH_TESTS to OFF before building installer package for size optimization.
	option(BUILD_WITH_TESTS "Enable testing" ON)
else()
	set(CMAKE_CXX_STANDARD 17)
	# Use shared libraries for AWS SDK.
	option(BUILD_SHARED_LIBS "Build shared libraries" ON)
	# Set BUILD_WITH_TESTS to OFF before building installer package for size optimization.
	option(BUILD_WITH_TESTS "Enable testing" ON)
endif()

if(MSVC)
  add_compile_options(/W4 /WX)
else()
  add_compile_options(-Wall -Wextra -pedantic -Werror -Wno-strict-prototypes -Wno-deprecated-declarations)
endif()

if(NOT WIN32)
	# Unix builds require autoconf
	option(AUTOCONF_ENABLE "Enable autoconf" ON)
	configure_file(autoconf.h.in generated/autoconf.h @ONLY)
endif()

# Base directories
set(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(OPENSEARCHODBC_SRC "${CMAKE_CURRENT_SOURCE_DIR}/sqlodbc")
set(OPENSEARCHENLIST_SRC "${CMAKE_CURRENT_SOURCE_DIR}/opensearchenlist")
set(INTEGRATION_TESTS "${CMAKE_CURRENT_SOURCE_DIR}/IntegrationTests")
set(UNIT_TESTS "${CMAKE_CURRENT_SOURCE_DIR}/UnitTests")
set(LIBRARY_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../libraries)
set(INSTALL_SRC "${CMAKE_CURRENT_SOURCE_DIR}/installer")
set(DSN_INSTALLER_SRC "${CMAKE_CURRENT_SOURCE_DIR}/DSNInstaller")

# ODBC Driver version
set(DRIVER_PACKAGE_VERSION "1.6.0.0")
set(DRIVER_PACKAGE_VERSION_COMMA_SEPARATED "1,6,0,0")
add_compile_definitions( OPENSEARCH_ODBC_VERSION="${DRIVER_PACKAGE_VERSION}"
                         # Comma separated version is required for odbc administrator's driver file.
                         OPENSEARCH_ODBC_DRVFILE_VERSION=${DRIVER_PACKAGE_VERSION_COMMA_SEPARATED} )

# Extensions of base directories
set(PERFORMANCE_TESTS "${CMAKE_CURRENT_SOURCE_DIR}/PerformanceTests")
set(UT_HELPER "${UNIT_TESTS}/UTHelper")
set(IT_HELPER "${INTEGRATION_TESTS}/ITODBCHelper")
set(RABBIT_SRC ${LIBRARY_DIRECTORY}/rabbit/include)
set(VLD_SRC ${LIBRARY_DIRECTORY}/VisualLeakDetector/include)
if(WIN32)
    set(RAPIDJSON_SRC ${LIBRARY_DIRECTORY}/rapidjson/include)
endif ()
# Without this symbols will be exporting to Unix but not Windows
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS TRUE)

find_package(aws-cpp-sdk-core CONFIG REQUIRED)
if(APPLE)
    find_package(ZLIB REQUIRED)
    find_package(RapidJSON CONFIG REQUIRED)
endif()

# General compiler definitions
add_compile_definitions		(	_SILENCE_TR1_NAMESPACE_DEPRECATION_WARNING
								UNICODE_SUPPORT
								DYNAMIC_LOAD
								_MBCS
								_CRT_SECURE_NO_DEPRECATE
								_USRDLL
								_SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING
								_SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
								# USE_SSL
							)

# Platform specific compiler definitions
if (WIN32 AND BITNESS EQUAL 64)
	# Windows specific
	add_compile_definitions	(	_WINDLL
								_WIN64
								_WINDOWS
								WIN_EXPORT
							)
elseif (WIN32 AND BITNESS EQUAL 32)
	# Windows specific
	add_compile_definitions	(	_WINDLL
								_WIN32
								_WINDOWS
								WIN_EXPORT
							)
elseif(APPLE)
	# macOS specific
	add_compile_definitions	(	WITH_IODBC
								SQLCOLATTRIBUTE_SQLLEN
							)
	# iODBC includes
	include_directories(/usr/local/opt/libiodbc/include)
elseif(UNIX)
	# Unix specific
	add_compile_definitions	(	WITH_UNIXODBC
								SQLCOLATTRIBUTE_SQLLEN
							)
endif()

if(BUILD_WITH_TESTS)
	# GTest import
	enable_testing()
endif()

# Projects to build
add_subdirectory(${OPENSEARCHODBC_SRC})
add_subdirectory(${OPENSEARCHENLIST_SRC})
add_subdirectory(${INSTALL_SRC})

# Only build & package DSN installer for Mac platforms
if(APPLE)
	add_subdirectory(${DSN_INSTALLER_SRC})
endif()

if(BUILD_WITH_TESTS)
	add_subdirectory(${INTEGRATION_TESTS})
	add_subdirectory(${UNIT_TESTS})
	add_subdirectory(${PERFORMANCE_TESTS})
endif()
