# Last Chaos client build. Replaces Build_2010.sln (VS2010).
#
# Today this builds the original Direct3D 8 client as 32-bit Windows (MSVC x86), so the
# client and server protocols match by construction while the macOS port is in progress.
#
#   cmake -S . -B build-client -G Ninja -DCMAKE_BUILD_TYPE=Release -DLC_BUILD_SERVER=OFF -DLC_BUILD_CLIENT=ON
#         -DLC_D3DX9_DIR=<Microsoft.DXSDK.D3DX package>/build/native -DLC_D3D8_INCLUDE_DIR=<dir with d3d8.h>
#   (from an "x86 Native Tools" developer prompt)

option(LC_CLIENT_CHECK_ONLY "Only compile (no link) the portable Engine sources, to find 64-bit and portability errors" OFF)
if(LC_CLIENT_CHECK_ONLY)
  include(cmake/ClientCheck.cmake)
  return()
endif()

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "The client currently builds only as 32-bit Windows (MSVC x86); see PORTING_MACOS.md")
endif()

set(LC_CLIENT_COUNTRY "BRAZIL" CACHE STRING "Client country define (G_<COUNTRY>): BRAZIL, USA, GERMAN, KOR, RUSSIA, THAI, ...")
option(LC_NO_IP_FILTER "Let the client connect to any login server address (otherwise only the publisher's ranges)" ON)
set(LC_D3DX9_DIR "" CACHE PATH "Microsoft.DXSDK.D3DX NuGet package 'build/native' directory")
# The Windows SDK no longer ships the Direct3D 8 headers; the MinGW-w64 (Wine) d3d8.h,
# d3d8caps.h and d3d8types.h work. D3D8.DLL itself is loaded at runtime by the engine.
set(LC_D3D8_INCLUDE_DIR "" CACHE PATH "Directory with d3d8.h, d3d8caps.h and d3d8types.h")
if(NOT EXISTS "${LC_D3D8_INCLUDE_DIR}/d3d8.h")
  message(FATAL_ERROR "Set LC_D3D8_INCLUDE_DIR to a directory containing d3d8.h (e.g. from mingw-w64-headers)")
endif()
if(NOT EXISTS "${LC_D3DX9_DIR}/include/d3dx9shader.h")
  message(FATAL_ERROR "Set LC_D3DX9_DIR to the Microsoft.DXSDK.D3DX package's build/native directory")
endif()

# Boost: Asio, Thread, Function, Format and Multi-index (as in the server).
find_package(Boost 1.83 CONFIG REQUIRED COMPONENTS thread system)

set(LC_CLIENT_SRC "${CMAKE_CURRENT_SOURCE_DIR}")
set(LC_GEN "${CMAKE_CURRENT_BINARY_DIR}/gen")
set(LC_BIN "${CMAKE_BINARY_DIR}/Bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${LC_BIN}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${LC_BIN}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
set(CMAKE_CXX_STANDARD 17)

# Settings shared by every client target (from the BRZLIVE configuration of the .vcxproj files).
add_library(lc_client_common INTERFACE)
target_compile_definitions(lc_client_common INTERFACE
  G_${LC_CLIENT_COUNTRY} _CLIENT_ _USE_32BIT_TIME_T WIN32 NDEBUG _WINDOWS _MBCS
  LC_NO_XTRAP NO_GAMEGUARD
  _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS _WINSOCK_DEPRECATED_NO_WARNINGS
  _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS _HAS_STD_BYTE=0)
if(LC_NO_IP_FILTER)
  target_compile_definitions(lc_client_common INTERFACE LC_NO_IP_FILTER)
endif()
# SE_CheckEngine() compares the binaries against data/etc/binary.lod (the publisher's
# build) and quits silently on a mismatch, so it must be off for binaries built here.
target_compile_definitions(lc_client_common INTERFACE LC_NO_BINARY_CHECK)
target_compile_options(lc_client_common INTERFACE /W1 /we4172 /Zi /Oy- /MP /EHsc /permissive /Zc:strictStrings- /Zc:threadSafeInit- /bigobj
  "/FI${CMAKE_CURRENT_SOURCE_DIR}/Compat/d3d8_mingw.h")  # what the MinGW-w64 d3d8 headers expect
target_include_directories(lc_client_common INTERFACE
  "${LC_GEN}" "${LC_CLIENT_SRC}" "${LC_CLIENT_SRC}/include" "${LC_CLIENT_SRC}/Compat/d3dx8")
target_include_directories(lc_client_common SYSTEM INTERFACE "${LC_D3D8_INCLUDE_DIR}")
target_link_libraries(lc_client_common INTERFACE Boost::headers)
# Debug info and frame pointers so crash stacks (ErrorLog.txt, debuggers) are usable.
target_link_options(lc_client_common INTERFACE /DEBUG /OPT:REF /OPT:ICF)
# Libraries requested by name through #pragma comment(lib, ...) live in the archive directory.
target_link_directories(lc_client_common INTERFACE "${CMAKE_BINARY_DIR}/lib")

# --- Third-party libraries, built from source (the prebuilt Lib/VC100/*.lib are VS2010-only) ---

# Engine/StdH.h asks for these by name with #pragma comment(lib, "zlib.lib"/"libpng.lib").
set(LC_ZLIB_DIR "${LC_CLIENT_SRC}/3rdparty/zlib-1.2.8")
add_library(zlib STATIC
  ${LC_ZLIB_DIR}/adler32.c ${LC_ZLIB_DIR}/compress.c ${LC_ZLIB_DIR}/crc32.c ${LC_ZLIB_DIR}/deflate.c
  ${LC_ZLIB_DIR}/gzclose.c ${LC_ZLIB_DIR}/gzlib.c ${LC_ZLIB_DIR}/gzread.c ${LC_ZLIB_DIR}/gzwrite.c
  ${LC_ZLIB_DIR}/infback.c ${LC_ZLIB_DIR}/inffast.c ${LC_ZLIB_DIR}/inflate.c ${LC_ZLIB_DIR}/inftrees.c
  ${LC_ZLIB_DIR}/trees.c ${LC_ZLIB_DIR}/uncompr.c ${LC_ZLIB_DIR}/zutil.c)
target_include_directories(zlib PUBLIC "${LC_ZLIB_DIR}")
target_compile_definitions(zlib PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)

set(LC_PNG_DIR "${LC_CLIENT_SRC}/3rdparty/lpng163")
configure_file("${LC_PNG_DIR}/scripts/pnglibconf.h.prebuilt" "${LC_GEN}/libpng/pnglibconf.h" COPYONLY)
add_library(libpng STATIC
  ${LC_PNG_DIR}/png.c ${LC_PNG_DIR}/pngerror.c ${LC_PNG_DIR}/pngget.c ${LC_PNG_DIR}/pngmem.c
  ${LC_PNG_DIR}/pngpread.c ${LC_PNG_DIR}/pngread.c ${LC_PNG_DIR}/pngrio.c ${LC_PNG_DIR}/pngrtran.c
  ${LC_PNG_DIR}/pngrutil.c ${LC_PNG_DIR}/pngset.c ${LC_PNG_DIR}/pngtrans.c ${LC_PNG_DIR}/pngwio.c
  ${LC_PNG_DIR}/pngwrite.c ${LC_PNG_DIR}/pngwtran.c ${LC_PNG_DIR}/pngwutil.c)
target_include_directories(libpng PUBLIC "${LC_PNG_DIR}" "${LC_GEN}/libpng")
target_compile_definitions(libpng PRIVATE _CRT_SECURE_NO_WARNINGS)
target_link_libraries(libpng PUBLIC zlib)

add_library(tinyxml STATIC
  3rdparty/tinyxml_2_6_2/tinyxml.cpp 3rdparty/tinyxml_2_6_2/tinystr.cpp
  3rdparty/tinyxml_2_6_2/tinyxmlerror.cpp 3rdparty/tinyxml_2_6_2/tinyxmlparser.cpp)
target_link_libraries(tinyxml PRIVATE lc_client_common)

# LCCrypt ships only as a VS2010 .lib on the client side; the same source lives in the server's ShareLib.
add_library(lccrypt STATIC "${LC_CLIENT_SRC}/../Server Source/ShareLib/LCCrypt.cpp")
target_include_directories(lccrypt PUBLIC "${LC_CLIENT_SRC}/../Server Source/ShareLib")
target_link_libraries(lccrypt PRIVATE lc_client_common)
set_target_properties(lccrypt PROPERTIES OUTPUT_NAME LCCrypt_mt)  # #pragma comment(lib, "LCCrypt_mt.lib")

add_library(f_socket STATIC
  F_Socket/F_Buffer_L.cpp F_Socket/F_CSocket.cpp F_Socket/F_CSocket_Event.cpp
  F_Socket/F_Critical.cpp F_Socket/F_Thread.cpp F_Socket/F_WSThread.cpp)
target_link_libraries(f_socket PRIVATE lc_client_common)

add_library(SharedMemory STATIC  # requested as "SharedMemory.lib" by Nksp
  SharedMemory/Access_control.cpp SharedMemory/Ext_ipc_event.cpp SharedMemory/SharedMemory_queue.cpp)
target_link_libraries(SharedMemory PRIVATE lc_client_common)

add_library(d3dx8_compat STATIC Compat/d3dx8/d3dx8_tex.cpp Compat/d3dx8/d3dx8_shader.cpp)
set_target_properties(d3dx8_compat PROPERTIES OUTPUT_NAME d3dx8)  # #pragma comment(lib, "d3dx8.lib")
target_include_directories(d3dx8_compat PRIVATE "${LC_D3DX9_DIR}/include")
target_link_libraries(d3dx8_compat PRIVATE lc_client_common
  PUBLIC "${LC_D3DX9_DIR}/release/lib/x86/d3dx9.lib")

include(cmake/Ecc.cmake)

include(cmake/Engine_sources.cmake)
include(cmake/EntitiesMP_sources.cmake)
include(cmake/GameMP_sources.cmake)
include(cmake/Shaders_sources.cmake)
include(cmake/Nksp_sources.cmake)

foreach(dir Engine/Classes EntitiesMP)
  file(MAKE_DIRECTORY "${LC_GEN}/${dir}")
endforeach()
lc_generate_entities(ENGINE_GENERATED Engine ${ENGINE_ES} LINE)
lc_generate_entities(ENTITIESMP_GENERATED EntitiesMP ${ENTITIESMP_ES})
add_custom_target(lc_entities DEPENDS ${ENGINE_GENERATED} ${ENTITIESMP_GENERATED})

# --- Engine, Shaders, EntitiesMP, GameMP and the game executable (Nksp) ---

function(lc_client_target name type)
  add_library(${name} ${type} ${ARGN})
  target_link_libraries(${name} PRIVATE lc_client_common)
  target_include_directories(${name} PRIVATE "${LC_CLIENT_SRC}/${name}")
  add_dependencies(${name} lc_entities)
endfunction()

lc_client_target(Engine SHARED ${ENGINE_SOURCES} ${ENGINE_GENERATED} Engine/Engine.def)
target_compile_definitions(Engine PRIVATE ENGINE_EXPORTS _USRDLL)
target_include_directories(Engine PRIVATE "${LC_GEN}/Engine")
# Intel JPEG Library 1.5 is a prebuilt C library (no C++ ABI), requested as "ijl15L.lib".
configure_file("${LC_CLIENT_SRC}/Lib/VC100/IJL15L.LIB" "${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}/ijl15L.lib" COPYONLY)
target_link_directories(Engine PRIVATE "${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}")
target_link_libraries(Engine PRIVATE zlib libpng tinyxml lccrypt d3dx8_compat Boost::thread Boost::system
  dsound winmm ws2_32 wininet imm32 version advapi32 odbc32 odbccp32 dbghelp)
target_link_options(Engine PRIVATE /MAP /ignore:4197 /NODEFAULTLIB:libci.lib /NODEFAULTLIB:LIBCMT.lib /NODEFAULTLIB:libc.lib)

lc_client_target(Shaders SHARED ${SHADERS_SOURCES})
target_compile_definitions(Shaders PRIVATE SHADERS_EXPORTS _USRDLL)
target_link_libraries(Shaders PRIVATE Engine)

lc_client_target(EntitiesMP SHARED ${ENTITIESMP_SOURCES} ${ENTITIESMP_GENERATED})
target_compile_definitions(EntitiesMP PRIVATE _WINDLL _USRDLL)
# StdH/StdH.h includes "..\\Global.h", which Ecc generates from Global.es into ${LC_GEN}/EntitiesMP:
# searching from ${LC_GEN}/EntitiesMP/StdH resolves it there.
file(MAKE_DIRECTORY "${LC_GEN}/EntitiesMP/StdH")
target_include_directories(EntitiesMP PRIVATE "${LC_GEN}/EntitiesMP" "${LC_CLIENT_SRC}/EntitiesMP/StdH" "${LC_GEN}/EntitiesMP/StdH")
target_link_libraries(EntitiesMP PRIVATE Engine user32 ws2_32 winmm)

lc_client_target(GameMP SHARED ${GAMEMP_SOURCES})
target_compile_definitions(GameMP PRIVATE GAME_EXPORTS _WINDLL _USRDLL)
target_link_libraries(GameMP PRIVATE Engine EntitiesMP user32)

add_executable(Nksp WIN32 ${NKSP_SOURCES} Nksp/Nksp.rc)
target_link_libraries(Nksp PRIVATE lc_client_common Engine EntitiesMP f_socket SharedMemory winmm ws2_32 version odbc32 odbccp32)
target_include_directories(Nksp PRIVATE "${LC_CLIENT_SRC}/Nksp")
target_link_options(Nksp PRIVATE /NODEFAULTLIB:game.lib /NODEFAULTLIB:gamemp.lib)
add_dependencies(Nksp lc_entities Shaders GameMP)

# Development check: which vertex shaders the D3D8 runtime accepts from the D3DX8 shim.
add_executable(ShaderCheck Compat/d3dx8/ShaderCheck.cpp)
target_link_libraries(ShaderCheck PRIVATE lc_client_common d3dx8_compat user32)

# Command-line exporter of the GameServer's zone maps (.sat/.sht) from client worlds.
add_executable(ZoneMapExport ZoneMapExport/ZoneMapExport.cpp)
target_link_libraries(ZoneMapExport PRIVATE lc_client_common Engine)
add_dependencies(ZoneMapExport lc_entities)

# D3DX9 runtime next to the executable.
add_custom_command(TARGET Nksp POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LC_D3DX9_DIR}/release/bin/x86/D3DX9_43.dll" "${LC_BIN}")
