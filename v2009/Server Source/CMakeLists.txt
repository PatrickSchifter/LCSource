# Last Chaos 2009 server build (macOS arm64 / Linux, clang or gcc).
#
#   cmake -S "v2009/Server Source" -B v2009/build-server -G Ninja
#   ninja -C v2009/build-server
#
# The 2009 tree has no Makefiles: every server directory is self-contained (each keeps its own
# copy of the CircleMUD-era shared files) and only LCCrypt is shared, as a static library.
cmake_minimum_required(VERSION 3.24)
project(LastChaos2009Server LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Region the code is compiled for (Config.h / Config_Localize.inh test LC_<REGION>).
set(LC_REGION "USA" CACHE STRING "Target region (KOR, TWN, CHN, TLD, JPN, MAL, USA, BRZ, HBK, GER)")
set(_lc_region_ids KOR=0 TWN=1 CHN=3 TLD=4 JPN=6 MAL=7 USA=9 BRZ=10 HBK=11 GER=13)
set(LC_REGION_DEFINE "")
foreach(_pair IN LISTS _lc_region_ids)
  string(REPLACE "=" ";" _kv "${_pair}")
  list(GET _kv 0 _name)
  list(GET _kv 1 _id)
  if(_name STREQUAL LC_REGION)
    set(LC_REGION_DEFINE "LC_${_name}=${_id}")
  endif()
endforeach()
if(NOT LC_REGION_DEFINE)
  message(FATAL_ERROR "Unknown LC_REGION '${LC_REGION}'")
endif()
message(STATUS "Last Chaos 2009 region: ${LC_REGION_DEFINE}")

find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
find_package(PkgConfig REQUIRED)

# MySQL client: Homebrew installs mysql-client keg-only, so pkg-config does not see it by default.
find_program(BREW_EXECUTABLE brew)
if(BREW_EXECUTABLE)
  execute_process(COMMAND ${BREW_EXECUTABLE} --prefix mysql-client
    OUTPUT_VARIABLE _keg_prefix OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  if(EXISTS "${_keg_prefix}/lib/pkgconfig")
    set(ENV{PKG_CONFIG_PATH} "${_keg_prefix}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
  endif()
endif()
pkg_check_modules(MYSQL REQUIRED IMPORTED_TARGET mysqlclient)
# The sources include <mysql/mysql.h>; pkg-config points at .../include/mysql itself.
set(_mysql_parent_includes "")
foreach(_dir IN LISTS MYSQL_INCLUDE_DIRS)
  get_filename_component(_parent "${_dir}" DIRECTORY)
  list(APPEND _mysql_parent_includes "${_parent}")
endforeach()

add_library(lc2009_common INTERFACE)
target_compile_definitions(lc2009_common INTERFACE _THREAD_SAFE ${LC_REGION_DEFINE})
target_compile_options(lc2009_common INTERFACE
  -Wno-write-strings
  -Wno-char-subscripts
  -Wno-register          # gcc-3/4 era code still uses the `register` keyword
  -Wno-invalid-source-encoding   # comments and string literals are CP949
  -Werror=return-type    # falling off a non-void function is UB that clang exploits
)
target_include_directories(lc2009_common INTERFACE
  ${_mysql_parent_includes}
  "${CMAKE_CURRENT_SOURCE_DIR}/LCCrypt")
target_link_libraries(lc2009_common INTERFACE PkgConfig::MYSQL ZLIB::ZLIB Threads::Threads ${CMAKE_DL_LIBS})

# Packet encryption (CRYPT_NET_MSG); was linked as a prebuilt library.
add_library(LCCrypt STATIC LCCrypt/LCCrypt.cpp LCCrypt/LCCrypt_ExternFunc.cpp)
target_include_directories(LCCrypt PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/LCCrypt")

# Each server is every *.cpp in its directory.
function(lc_add_server name)
  file(GLOB _sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${name}/*.cpp")
  add_executable(${name} ${_sources})
  target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/${name}")
  target_link_libraries(${name} PRIVATE lc2009_common LCCrypt)
endfunction()

lc_add_server(Messenger)
lc_add_server(Helper)
lc_add_server(Connector)
lc_add_server(LoginServer)
lc_add_server(GameServer)
