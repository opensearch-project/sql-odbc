project(dsn_installer)

set(SOURCE_FILES dsn_installer.cpp)

add_executable(dsn_installer ${SOURCE_FILES})

target_link_libraries(dsn_installer ${IODBCINST_LIB})
target_compile_definitions(dsn_installer PUBLIC _UNICODE UNICODE)
