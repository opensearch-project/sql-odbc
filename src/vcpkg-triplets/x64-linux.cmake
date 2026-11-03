# Overlay of vcpkg's built-in x64-linux triplet.
#
# Everything stays static, except unixODBC: it is LGPL-2.1 and must be linked
# dynamically so none of its code is copied into libsqlodbc.so. At runtime the
# driver uses the system's (or the application's) unixODBC driver manager.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)

if(PORT STREQUAL "unixodbc")
    set(VCPKG_LIBRARY_LINKAGE dynamic)
endif()
