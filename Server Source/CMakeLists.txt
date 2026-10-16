cmake_policy(SET CMP0167 NEW)  # use BoostConfig.cmake, not the removed FindBoost

# Server build. Replaces the per-region Makefiles / makefile.inc (gcc 4.4, 32-bit CentOS).

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Boost 1.83 REQUIRED COMPONENTS thread system)
find_package(PkgConfig REQUIRED)

# Homebrew installs mysql-client keg-only, so pkg-config does not see it by default.
find_program(BREW_EXECUTABLE brew)
if(BREW_EXECUTABLE)
  foreach(_keg mysql-client)
    execute_process(COMMAND ${BREW_EXECUTABLE} --prefix ${_keg}
      OUTPUT_VARIABLE _keg_prefix OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(EXISTS "${_keg_prefix}/lib/pkgconfig")
      set(ENV{PKG_CONFIG_PATH} "${_keg_prefix}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
    endif()
  endforeach()
endif()

pkg_check_modules(MYSQL   REQUIRED IMPORTED_TARGET mysqlclient)
pkg_check_modules(LOG4CXX REQUIRED IMPORTED_TARGET liblog4cxx)
pkg_check_modules(APR     REQUIRED IMPORTED_TARGET apr-1 apr-util-1)
pkg_check_modules(JSONCPP REQUIRED IMPORTED_TARGET jsoncpp)
pkg_check_modules(EXPAT   REQUIRED IMPORTED_TARGET expat)
pkg_check_modules(BOTAN   REQUIRED IMPORTED_TARGET botan-3)
find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)

# Git revision stands in for the old svn revision (svn_info.h, LC_VER).
execute_process(
  COMMAND git rev-parse --short HEAD
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  OUTPUT_VARIABLE LC_GIT_REV OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)

# Flags every server target shares (was CXXFLAGS/OPT_LDOBJ in makefile.inc).
add_library(lc_server_common INTERFACE)
target_compile_definitions(lc_server_common INTERFACE
  _THREAD_SAFE
  ${LC_REGION_DEFINE}
  LC_VER="macos-${LC_GIT_REV}"
)
target_compile_options(lc_server_common INTERFACE
  -Wno-write-strings
  -Wno-char-subscripts
  -Wno-register          # gcc-4 era code still uses the `register` keyword
  -Werror=return-type   # falling off a non-void function is UB that clang exploits
)
target_include_directories(lc_server_common INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/ShareLib")
target_link_libraries(lc_server_common INTERFACE
  Boost::thread Boost::system
  PkgConfig::MYSQL PkgConfig::LOG4CXX PkgConfig::APR PkgConfig::JSONCPP PkgConfig::EXPAT
  CURL::libcurl OpenSSL::SSL OpenSSL::Crypto
  ZLIB::ZLIB Threads::Threads ${CMAKE_DL_LIBS}
)

# svn_info.h used to be generated from `svn info`; take the git revision instead.
string(TIMESTAMP LC_COMPILE_TIME "%s" UTC)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/svn_info.h"
  "static const char* last_svn_revision=\"Rev: ${LC_GIT_REV}\";\n"
  "static const int compile_time = ${LC_COMPILE_TIME} ^ 0x12345678;\n")

file(GLOB SHARELIB_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ShareLib/*.cpp")
add_library(ShareLib STATIC ${SHARELIB_SOURCES})
target_include_directories(ShareLib PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(ShareLib PUBLIC lc_server_common PRIVATE PkgConfig::BOTAN)
# Botan 3 headers require C++20; keep the rest of the legacy code on C++17.
set_source_files_properties(ShareLib/CheckPassword.cpp PROPERTIES COMPILE_OPTIONS "-std=c++20")

# Each server is every *.cpp in its directory, as with the old `$(wildcard *.cpp)`.
function(lc_add_server name)
  cmake_parse_arguments(ARG "" "" "DEFINES;LIBS" ${ARGN})
  file(GLOB _sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${name}/*.cpp")
  add_executable(${name} ${_sources})
  target_compile_definitions(${name} PRIVATE ${ARG_DEFINES})
  target_link_libraries(${name} PRIVATE ShareLib ${ARG_LIBS})
endfunction()

lc_add_server(Messenger)
lc_add_server(Helper)
lc_add_server(SubHelper)
lc_add_server(Connector)
lc_add_server(LoginServer)
lc_add_server(GameServer DEFINES __GAME_SERVER__)
