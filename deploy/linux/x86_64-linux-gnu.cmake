# CMake toolchain for building the servers for x86_64 Linux from another Debian architecture
# (deploy/linux/Dockerfile: the build runs natively on arm64, with the amd64 libraries installed
# through Debian multiarch and Debian's cross gcc).
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER x86_64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER x86_64-linux-gnu-g++)
set(CMAKE_LIBRARY_ARCHITECTURE x86_64-linux-gnu)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(ENV{PKG_CONFIG_LIBDIR} "/usr/lib/x86_64-linux-gnu/pkgconfig:/usr/share/pkgconfig")
