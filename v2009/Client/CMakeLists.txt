# Last Chaos 2009 client build (v2009/Client): the ported low-level engine of Client Source/ with
# the 2009 game layer (Engine/Interface, Network, Entities, EntitiesMP, GameMP).
#
#   cmake -S v2009/Client -B v2009/build-client -G Ninja -DCMAKE_BUILD_TYPE=Release
#
# What follows is the build of Client Source/ (the 2015 client), adapted:
#
# Two builds of the same sources:
# - Windows (MSVC x86, 32 bits): the original Direct3D 8 client, the reference for the port and
#   the client that tests the server.
#
#   cmake -S . -B build-client -G Ninja -DCMAKE_BUILD_TYPE=Release -DLC_BUILD_SERVER=OFF -DLC_BUILD_CLIENT=ON
#         -DLC_D3DX9_DIR=<Microsoft.DXSDK.D3DX package>/build/native -DLC_D3D8_INCLUDE_DIR=<dir with d3d8.h>
#   (from an "x86 Native Tools" developer prompt)
#
# - macOS (clang, arm64): OpenGL, SDL3 for the window, events and sound (PORTING_MACOS.md).
#   Homebrew packages from scripts/macos-deps.sh.
#
#   cmake -S . -B build-client-mac -G Ninja -DCMAKE_BUILD_TYPE=Release -DLC_BUILD_SERVER=OFF -DLC_BUILD_CLIENT=ON

# Region (country) the client is compiled for, shared by every client build (Windows, the macOS
# check and the macOS build). G_<COUNTRY> picks the publisher's feature set in
# Engine/LocalDefine.h (Engine/Define_<...>.h) and a few G_<COUNTRY> branches in the code (font
# and text handling for RUSSIA/THAI, the codepage outside Windows in Base/Win32Compat.cpp).
# The language data used at run time (Local\<nation>\, fonts) follows the persistent shell
# symbol g_iCountry instead (BRAZIL = 8, see Engine/GlobalDefinition.h), set by the installation.
# The server's LC_REGION is a different list (BRZ, USA, GER...) and is set separately.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.24)
  project(LastChaos2009Client LANGUAGES C CXX)
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

set(LC_CLIENT_COUNTRY "USA" CACHE STRING "Client country define (G_<COUNTRY>): BRAZIL, USA, GERMAN, KOR, RUSSIA, THAI")
set(_lc_client_countries BRAZIL USA GERMAN KOR RUSSIA THAI)  # the ones Engine/LocalDefine.h knows
set_property(CACHE LC_CLIENT_COUNTRY PROPERTY STRINGS ${_lc_client_countries})
if(NOT LC_CLIENT_COUNTRY IN_LIST _lc_client_countries)
  string(REPLACE ";" ", " _list "${_lc_client_countries}")
  message(FATAL_ERROR "Unknown LC_CLIENT_COUNTRY '${LC_CLIENT_COUNTRY}' (one of: ${_list})")
endif()
message(STATUS "Last Chaos client country: G_${LC_CLIENT_COUNTRY}")
# Defines every client build shares.
set(LC_CLIENT_DEFINES G_${LC_CLIENT_COUNTRY} _CLIENT_)

option(LC_CLIENT_CHECK_ONLY "Only compile (no link) the portable Engine sources, to find 64-bit and portability errors" OFF)
if(LC_CLIENT_CHECK_ONLY)
  include(cmake/ClientCheck.cmake)
  return()
endif()

if(WIN32)
  if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "The Windows client builds only as 32-bit (MSVC x86); see PORTING_MACOS.md")
  endif()
elseif(NOT APPLE)
  message(FATAL_ERROR "The client builds on Windows (MSVC x86) and macOS; see PORTING_MACOS.md")
endif()

option(LC_NO_IP_FILTER "Let the client connect to any login server address (otherwise only the publisher's ranges)" ON)

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

# Settings shared by every client target. Without the .vcxproj's _USE_32BIT_TIME_T: time_t is
# 64-bit on both platforms; times the game keeps in 32 bits go through LocalTime32()
# (Engine/Base/Types.h).
add_library(lc_client_common INTERFACE)
target_compile_definitions(lc_client_common INTERFACE ${LC_CLIENT_DEFINES} NDEBUG)
if(LC_NO_IP_FILTER)
  target_compile_definitions(lc_client_common INTERFACE LC_NO_IP_FILTER)
endif()
# SE_CheckEngine() compares the binaries against data/etc/binary.lod (the publisher's
# build) and quits silently on a mismatch, so it must be off for binaries built here.
target_compile_definitions(lc_client_common INTERFACE LC_NO_BINARY_CHECK)
# The 2009 sources build nProtect GameGuard unless NO_GAMEGUARD is defined; it is Windows-only and
# talks to the publisher's servers.
target_compile_definitions(lc_client_common INTERFACE NO_GAMEGUARD)
target_include_directories(lc_client_common INTERFACE
  "${LC_GEN}" "${LC_CLIENT_SRC}" "${LC_CLIENT_SRC}/include" "${LC_CLIENT_SRC}/Compat/d3dx8"
  "${LC_CLIENT_SRC}/LCCrypt")  # Engine/Network/CNetwork.h includes <LCCrypt.h>
target_link_libraries(lc_client_common INTERFACE Boost::headers)

if(WIN32)
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

  # From the BRZLIVE configuration of the .vcxproj files. SE1_D3D compiles in the Direct3D 8
  # backend (as in Croteam's Serious Engine 1 sources); without it (macOS) only OpenGL is left:
  # GAT_D3D, the D3D members and the D3D-only sources (wrapped in #ifdef SE1_D3D) are compiled out.
  target_compile_definitions(lc_client_common INTERFACE
    SE1_D3D WIN32 _WINDOWS _MBCS
    _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS _WINSOCK_DEPRECATED_NO_WARNINGS
    _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS _HAS_STD_BYTE=0)
  # The C paths that replace the x86 inline assembly (always used outside MSVC x86, see
  # Engine/Base/Base.h); turn this on to test them in the Windows client.
  option(LC_USE_PORTABLE_C "Use the C paths instead of the x86 inline assembly" OFF)
  if(LC_USE_PORTABLE_C)
    target_compile_definitions(lc_client_common INTERFACE USE_PORTABLE_C)
  endif()
  # The SDL3 platform layer (window, events) that the macOS client uses; turn this on to test it in
  # the Windows client (Direct3D 8 still draws, on the window SDL creates). Needs SDL3_DIR (the
  # 'cmake' directory of the SDL3 VC development package).
  option(LC_PLATFORM_SDL "Use the SDL3 platform layer instead of WinMain/WndProc" OFF)
  if(LC_PLATFORM_SDL)
    find_package(SDL3 CONFIG REQUIRED)
    target_compile_definitions(lc_client_common INTERFACE LC_PLATFORM_SDL)
    target_link_libraries(lc_client_common INTERFACE SDL3::SDL3)
  endif()
  # The SDL3 sound output that the macOS client uses (Engine/Sound/SoundLibrary.cpp), in place of
  # DirectSound/WaveOut; turn this on to test it in the Windows client. Needs LC_PLATFORM_SDL. Ogg
  # and MP3 still come from vorbisfile.dll and amp11lib.dll.
  option(LC_SOUND_SDL "Play sound through SDL3 instead of DirectSound/WaveOut" OFF)
  if(LC_SOUND_SDL)
    if(NOT LC_PLATFORM_SDL)
      message(FATAL_ERROR "LC_SOUND_SDL needs LC_PLATFORM_SDL")
    endif()
    target_compile_definitions(lc_client_common INTERFACE LC_SOUND_SDL)
  endif()
  target_compile_options(lc_client_common INTERFACE /W1 /we4172 /Zi /Oy- /MP /EHsc /permissive /Zc:strictStrings- /Zc:threadSafeInit- /bigobj
    "/FI${CMAKE_CURRENT_SOURCE_DIR}/Compat/d3d8_mingw.h")  # what the MinGW-w64 d3d8 headers expect
  target_include_directories(lc_client_common SYSTEM INTERFACE "${LC_D3D8_INCLUDE_DIR}")
  # Debug info and frame pointers so crash stacks (ErrorLog.txt, debuggers) are usable.
  target_link_options(lc_client_common INTERFACE /DEBUG /OPT:REF /OPT:ICF)
  # Libraries requested by name through #pragma comment(lib, ...) live in the archive directory.
  target_link_directories(lc_client_common INTERFACE "${CMAKE_BINARY_DIR}/lib")
else()
  # macOS: the SDL3 platform layer and sound (LC_PLATFORM_SDL and LC_SOUND_SDL are defined by
  # Engine/Base/Base.h for every non-MSVC compiler), Ogg Vorbis through libvorbisfile, text
  # code pages through iconv, OpenGL for the rendering (the context comes from SDL).
  find_package(SDL3 CONFIG REQUIRED)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(VORBISFILE REQUIRED IMPORTED_TARGET vorbisfile)
  find_package(Iconv REQUIRED)
  target_link_libraries(lc_client_common INTERFACE SDL3::SDL3)
  target_compile_definitions(lc_client_common INTERFACE _MT)  # MSVC's multithreaded CRT flag (Base/Timer.h)
  # The Metal backend (GAT_MTL, Graphics/MetalDevice.mm), the default; LC_GFX_API=opengl picks
  # OpenGL. SE1_GPUDEVICE: the gfx* functions on a GpuDevice (Graphics/Gfx_Gpu.cpp), Metal's and
  # Vulkan's. The Vulkan backend (GAT_VK, Graphics/VulkanDevice.mm, LC_GFX_API=vulkan) when the
  # Vulkan loader (with MoltenVK) and glslang are there (Homebrew: molten-vk vulkan-loader
  # vulkan-headers glslang); they are linked weakly, so the game starts without them.
  target_compile_definitions(lc_client_common INTERFACE SE1_METAL SE1_GPUDEVICE)
  find_package(Vulkan QUIET)
  find_package(glslang CONFIG QUIET)
  if(Vulkan_FOUND AND TARGET glslang::glslang AND TARGET glslang::glslang-default-resource-limits)
    set(LC_VULKAN ON)
    target_compile_definitions(lc_client_common INTERFACE SE1_VULKAN)
    message(STATUS "Vulkan backend: ${Vulkan_LIBRARY}")
  else()
    set(LC_VULKAN OFF)
    message(STATUS "Vulkan backend: off (no Vulkan loader or glslang)")
  endif()
  # -fms-extensions for the MSVC-isms the code still has (as in the check build). The rest keep
  # the semantics the code was written for under MSVC: signed overflow wraps, no type-based
  # aliasing, and 'this==NULL'-style checks are not optimized away. Warnings are off here: the
  # check build (LC_CLIENT_CHECK_ONLY) is where the 64-bit and portability ones are reviewed.
  # Except one, as an error: MSVC passes a CTString through '...' as its char pointer, clang
  # compiles such a call into a trap; pass (const char*)str instead.
  target_compile_options(lc_client_common INTERFACE
    -fms-extensions -fwrapv -fno-strict-aliasing -fno-delete-null-pointer-checks -g -Wno-everything
    -Werror=non-pod-varargs)
  set(CMAKE_BUILD_RPATH_USE_ORIGIN ON)
endif()

# --- Third-party libraries, built from source (the VS2010 prebuilt .lib files were removed) ---

# Engine/StdH.h asks for these by name with #pragma comment(lib, "zlib.lib"/"libpng.lib").
set(LC_ZLIB_DIR "${LC_CLIENT_SRC}/3rdparty/zlib-1.2.8")
add_library(zlib STATIC
  ${LC_ZLIB_DIR}/adler32.c ${LC_ZLIB_DIR}/compress.c ${LC_ZLIB_DIR}/crc32.c ${LC_ZLIB_DIR}/deflate.c
  ${LC_ZLIB_DIR}/gzclose.c ${LC_ZLIB_DIR}/gzlib.c ${LC_ZLIB_DIR}/gzread.c ${LC_ZLIB_DIR}/gzwrite.c
  ${LC_ZLIB_DIR}/infback.c ${LC_ZLIB_DIR}/inffast.c ${LC_ZLIB_DIR}/inflate.c ${LC_ZLIB_DIR}/inftrees.c
  ${LC_ZLIB_DIR}/trees.c ${LC_ZLIB_DIR}/uncompr.c ${LC_ZLIB_DIR}/zutil.c)
target_include_directories(zlib PUBLIC "${LC_ZLIB_DIR}")
target_compile_definitions(zlib PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)
if(APPLE)
  # zlib 1.2.8 takes TARGET_OS_MAC for the classic Mac OS: it would define fdopen() as NULL
  # (zutil.h skips that when fdopen is already a macro) and leave out <unistd.h>.
  target_compile_definitions(zlib PRIVATE Z_HAVE_UNISTD_H fdopen=fdopen)
endif()

set(LC_PNG_DIR "${LC_CLIENT_SRC}/3rdparty/lpng163")
configure_file("${LC_PNG_DIR}/scripts/pnglibconf.h.prebuilt" "${LC_GEN}/libpng/pnglibconf.h" COPYONLY)
add_library(libpng STATIC
  ${LC_PNG_DIR}/png.c ${LC_PNG_DIR}/pngerror.c ${LC_PNG_DIR}/pngget.c ${LC_PNG_DIR}/pngmem.c
  ${LC_PNG_DIR}/pngpread.c ${LC_PNG_DIR}/pngread.c ${LC_PNG_DIR}/pngrio.c ${LC_PNG_DIR}/pngrtran.c
  ${LC_PNG_DIR}/pngrutil.c ${LC_PNG_DIR}/pngset.c ${LC_PNG_DIR}/pngtrans.c ${LC_PNG_DIR}/pngwio.c
  ${LC_PNG_DIR}/pngwrite.c ${LC_PNG_DIR}/pngwtran.c ${LC_PNG_DIR}/pngwutil.c)
target_include_directories(libpng PUBLIC "${LC_PNG_DIR}" "${LC_GEN}/libpng")
target_compile_definitions(libpng PRIVATE _CRT_SECURE_NO_WARNINGS)
if(APPLE)
  # libpng 1.6.3 also takes TARGET_OS_MAC for the classic Mac OS and wants its <fp.h>, unless
  # <math.h> came first. Its ARM NEON filters live in arm/, which the Windows build does not
  # need: plain C filters on arm64 too.
  target_compile_options(libpng PRIVATE -include math.h)
  target_compile_definitions(libpng PRIVATE PNG_ARM_NEON_OPT=0)
endif()
target_link_libraries(libpng PUBLIC zlib)

add_library(tinyxml STATIC
  3rdparty/tinyxml_2_6_2/tinyxml.cpp 3rdparty/tinyxml_2_6_2/tinystr.cpp
  3rdparty/tinyxml_2_6_2/tinyxmlerror.cpp 3rdparty/tinyxml_2_6_2/tinyxmlparser.cpp)
target_link_libraries(tinyxml PRIVATE lc_client_common)

# LCCrypt: the 2009 message encryption (CNM_* functions, the same code as the 2009 server's
# LCCrypt), not the later DES3 one of the server's ShareLib.
add_library(lccrypt STATIC "${LC_CLIENT_SRC}/LCCrypt/LCCrypt.cpp" "${LC_CLIENT_SRC}/LCCrypt/LCCrypt_ExternFunc.cpp")
target_include_directories(lccrypt PUBLIC "${LC_CLIENT_SRC}/LCCrypt")
set_target_properties(lccrypt PROPERTIES OUTPUT_NAME LCCrypt)  # #pragma comment(lib, "LCCrypt.lib")

if(WIN32)
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
endif()

include(cmake/Ecc.cmake)
include(cmake/EngineParsers.cmake)

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

# The engine loads EntitiesMP, GameMP and Shaders at run time by file name ("Bin\X.dll"; outside
# Windows LoadLibrary() opens X.dylib, Base/Win32Compat.cpp), so the libraries keep their bare
# names on macOS too, and find Engine.dylib next to themselves.
function(lc_client_target name type)
  add_library(${name} ${type} ${ARGN})
  target_link_libraries(${name} PRIVATE lc_client_common)
  target_include_directories(${name} PRIVATE "${LC_CLIENT_SRC}/${name}")
  add_dependencies(${name} lc_entities)
  if(APPLE)
    set_target_properties(${name} PROPERTIES PREFIX "" INSTALL_NAME_DIR "@rpath" BUILD_RPATH "@loader_path")
  endif()
endfunction()

if(WIN32)
  set(_engine_def Engine/Engine.def)
endif()
lc_client_target(Engine SHARED ${ENGINE_SOURCES} ${ENGINE_GENERATED} ${ENGINE_PARSER_SOURCES} ${_engine_def})
target_compile_definitions(Engine PRIVATE ENGINE_EXPORTS _USRDLL)
target_include_directories(Engine PRIVATE "${LC_GEN}/Engine")
target_link_libraries(Engine PRIVATE zlib libpng tinyxml lccrypt Boost::thread Boost::system)
if(WIN32)
  target_link_libraries(Engine PRIVATE d3dx8_compat dsound winmm ws2_32 wininet imm32 version advapi32 dbghelp)
  target_link_options(Engine PRIVATE /MAP /ignore:4197 /NODEFAULTLIB:libci.lib /NODEFAULTLIB:LIBCMT.lib /NODEFAULTLIB:libc.lib)
else()
  target_link_libraries(Engine PRIVATE PkgConfig::VORBISFILE Iconv::Iconv "-framework OpenGL" "-framework AppKit"
    "-framework Metal" "-framework QuartzCore")
  # trackpad gestures and the Metal device (Objective-C++), and the backends on a GpuDevice: not
  # in the generated source lists (Gfx_wrapper_Gpu.cpp is included by Gfx_wrapper.cpp)
  target_sources(Engine PRIVATE Engine/Platform/PlatformMac.mm Engine/Graphics/MetalDevice.mm Engine/Graphics/Gfx_Gpu.cpp
    Engine/Graphics/ShaderARBTranslate.cpp)
  set_source_files_properties(Engine/Platform/PlatformMac.mm Engine/Graphics/MetalDevice.mm
    PROPERTIES COMPILE_OPTIONS "-xobjective-c++;-fobjc-arc")
  if(LC_VULKAN)
    target_sources(Engine PRIVATE Engine/Graphics/VulkanDevice.mm)
    set_source_files_properties(Engine/Graphics/VulkanDevice.mm PROPERTIES COMPILE_OPTIONS "-xobjective-c++;-fobjc-arc")
    target_include_directories(Engine PRIVATE ${Vulkan_INCLUDE_DIRS}
      $<TARGET_PROPERTY:glslang::glslang,INTERFACE_INCLUDE_DIRECTORIES>)
    get_filename_component(LC_VULKAN_LIBDIR "${Vulkan_LIBRARY}" DIRECTORY)
    get_target_property(LC_GLSLANG_LIB glslang::glslang IMPORTED_LOCATION_RELEASE)
    get_target_property(LC_GLSLANG_LIMITS_LIB glslang::glslang-default-resource-limits IMPORTED_LOCATION_RELEASE)
    target_link_options(Engine PRIVATE "LINKER:-weak_library,${Vulkan_LIBRARY}"
      "LINKER:-weak_library,${LC_GLSLANG_LIB}" "LINKER:-weak_library,${LC_GLSLANG_LIMITS_LIB}")
  endif()
endif()

lc_client_target(Shaders SHARED ${SHADERS_SOURCES})
target_compile_definitions(Shaders PRIVATE SHADERS_EXPORTS _USRDLL)
target_link_libraries(Shaders PRIVATE Engine)

lc_client_target(EntitiesMP SHARED ${ENTITIESMP_SOURCES} ${ENTITIESMP_GENERATED})
target_compile_definitions(EntitiesMP PRIVATE _WINDLL _USRDLL)
# StdH/StdH.h includes "../Global.h", which Ecc generates from Global.es into ${LC_GEN}/EntitiesMP:
# searching from ${LC_GEN}/EntitiesMP/StdH resolves it there.
file(MAKE_DIRECTORY "${LC_GEN}/EntitiesMP/StdH")
target_include_directories(EntitiesMP PRIVATE "${LC_GEN}/EntitiesMP" "${LC_CLIENT_SRC}/EntitiesMP/StdH" "${LC_GEN}/EntitiesMP/StdH")
target_link_libraries(EntitiesMP PRIVATE Engine)
if(WIN32)
  target_link_libraries(EntitiesMP PRIVATE user32 ws2_32 winmm)
endif()

lc_client_target(GameMP SHARED ${GAMEMP_SOURCES})
target_compile_definitions(GameMP PRIVATE GAME_EXPORTS _WINDLL _USRDLL)
target_link_libraries(GameMP PRIVATE Engine EntitiesMP)
if(WIN32)
  target_link_libraries(GameMP PRIVATE user32)
endif()

if(WIN32)
  add_executable(Nksp WIN32 ${NKSP_SOURCES} Nksp/Nksp.rc)
  target_link_libraries(Nksp PRIVATE lc_client_common Engine EntitiesMP f_socket SharedMemory winmm ws2_32 version)
  target_link_options(Nksp PRIVATE /NODEFAULTLIB:game.lib /NODEFAULTLIB:gamemp.lib)
else()
  add_executable(Nksp ${NKSP_SOURCES})
  target_link_libraries(Nksp PRIVATE lc_client_common Engine EntitiesMP)
  set_target_properties(Nksp PROPERTIES BUILD_RPATH "@executable_path")
endif()
target_include_directories(Nksp PRIVATE "${LC_CLIENT_SRC}/Nksp")
add_dependencies(Nksp lc_entities Shaders GameMP)

# The game's mouse cursors (the SDL platform layer reads them as files, not resources).
if(NOT WIN32 OR LC_PLATFORM_SDL)
  file(GLOB LC_CURSOR_FILES "${LC_CLIENT_SRC}/Nksp/res/cur_*.cur")
  add_custom_command(TARGET Nksp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${LC_BIN}/Cursors"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LC_CURSOR_FILES} "${LC_BIN}/Cursors")
endif()

# Command-line exporter of the GameServer's zone maps (.sat/.sht) from client worlds; on macOS too
# (v2009: to make the 2009 server's maps from the worlds this client loads).
if(NOT WIN32)
  add_executable(ZoneMapExport ZoneMapExport/ZoneMapExport.cpp)
  target_link_libraries(ZoneMapExport PRIVATE lc_client_common Engine)
  add_dependencies(ZoneMapExport lc_entities EntitiesMP)
  set_target_properties(ZoneMapExport PROPERTIES BUILD_RPATH "@executable_path")
endif()

if(WIN32)
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
  if(LC_PLATFORM_SDL)
    add_custom_command(TARGET Nksp POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:SDL3::SDL3>" "${LC_BIN}")
  endif()
endif()
