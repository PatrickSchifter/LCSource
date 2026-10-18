# LC_CLIENT_CHECK_ONLY: compiles, without linking, the Engine sources that do not depend on
# Direct3D or Win32 APIs, with the host compiler (clang on macOS arm64). It exists to find
# 64-bit and portability errors (PORTING_MACOS.md, C2) before the client is ported for real.
#
#   cmake -S . -B build-client-check -G Ninja -DLC_BUILD_SERVER=OFF -DLC_BUILD_CLIENT=ON -DLC_CLIENT_CHECK_ONLY=ON
#   cmake --build build-client-check -- -k 0
#
# Compat/checkonly holds empty stand-ins for Windows-only headers (windows.h, conio.h...), so
# each file gets past its includes and reports its own errors. They are for this check only.

set(LC_CLIENT_SRC "${CMAKE_CURRENT_SOURCE_DIR}")
set(LC_GEN "${CMAKE_CURRENT_BINARY_DIR}/gen")
set(CMAKE_CXX_STANDARD 17)

find_package(Boost 1.83 CONFIG REQUIRED)
find_package(SDL3 CONFIG REQUIRED)  # the platform layer outside Windows (Engine/Platform)

include(cmake/Ecc.cmake)
include(cmake/EngineParsers.cmake)
include(cmake/Engine_sources.cmake)

file(MAKE_DIRECTORY "${LC_GEN}/Engine/Classes")
lc_generate_entities(ENGINE_GENERATED Engine ${ENGINE_ES} LINE)
set(ENTITIESMP_GENERATED)
if(LC_CHECK_DIRS STREQUAL "ALL")
  include(cmake/EntitiesMP_sources.cmake)
  file(MAKE_DIRECTORY "${LC_GEN}/EntitiesMP/StdH")
  lc_generate_entities(ENTITIESMP_GENERATED EntitiesMP ${ENTITIESMP_ES})
endif()

# Engine directories to compile; "ALL" takes every Engine source (most of the rest needs
# Direct3D or Win32 and fails early, but still reports 64-bit warnings).
set(LC_CHECK_DIRS "Base;Math;Templates;Brushes;World;Terrain;Ska;Models" CACHE STRING "Engine directories for the client check, or ALL")
set(LC_CHECK_SOURCES)
foreach(src IN LISTS ENGINE_SOURCES)
  if(LC_CHECK_DIRS STREQUAL "ALL")
    if(src MATCHES "\\.cpp$")
      list(APPEND LC_CHECK_SOURCES "${src}")
    endif()
    continue()
  endif()
  foreach(dir IN LISTS LC_CHECK_DIRS)
    if(src MATCHES "^Engine/${dir}/[^/]+\\.cpp$")
      list(APPEND LC_CHECK_SOURCES "${src}")
    endif()
  endforeach()
endforeach()
list(APPEND LC_CHECK_SOURCES ${ENGINE_PARSER_SOURCES})
list(LENGTH LC_CHECK_SOURCES _n)
message(STATUS "Client check: ${_n} Engine sources")

add_custom_target(lc_engine_check_entities DEPENDS ${ENGINE_GENERATED} ${ENTITIESMP_GENERATED})
add_library(lc_engine_check OBJECT ${LC_CHECK_SOURCES})
add_dependencies(lc_engine_check lc_engine_check_entities)
target_compile_definitions(lc_engine_check PRIVATE _MT ${LC_CLIENT_DEFINES} NDEBUG)  # LC_CLIENT_COUNTRY: ../CMakeLists.txt
target_include_directories(lc_engine_check PRIVATE
  "${LC_GEN}" "${LC_GEN}/Engine" "${LC_CLIENT_SRC}" "${LC_CLIENT_SRC}/Engine" "${LC_CLIENT_SRC}/include"
  "${LC_CLIENT_SRC}/Compat/d3dx8")  # only its d3dx8math.h is used without SE1_D3D
target_include_directories(lc_engine_check SYSTEM PRIVATE "${LC_CLIENT_SRC}/Compat/checkonly")
target_link_libraries(lc_engine_check PRIVATE Boost::headers SDL3::Headers)
target_compile_options(lc_engine_check PRIVATE
  -fms-extensions -Wno-everything
  # what this check is for
  -Wshorten-64-to-32 -Wpointer-to-int-cast -Wint-to-pointer-cast -Wvoid-pointer-to-int-cast
  -Wformat -Wunsequenced
  -ferror-limit=0)

# With LC_CHECK_DIRS=ALL, the other client projects too (entity classes, game, shaders,
# executable).
if(LC_CHECK_DIRS STREQUAL "ALL")
  include(cmake/GameMP_sources.cmake)
  include(cmake/Shaders_sources.cmake)
  include(cmake/Nksp_sources.cmake)
  get_target_property(_defs lc_engine_check COMPILE_DEFINITIONS)
  get_target_property(_opts lc_engine_check COMPILE_OPTIONS)
  foreach(project IN ITEMS EntitiesMP GameMP Shaders Nksp)
    string(TOUPPER ${project} _up)
    set(_srcs ${${_up}_SOURCES})
    if(project STREQUAL "EntitiesMP")
      list(APPEND _srcs ${ENTITIESMP_GENERATED})
    endif()
    list(FILTER _srcs INCLUDE REGEX "\\.cpp$")
    set(_t lc_${project}_check)
    add_library(${_t} OBJECT ${_srcs})
    add_dependencies(${_t} lc_engine_check_entities)
    target_compile_definitions(${_t} PRIVATE ${_defs})
    target_compile_options(${_t} PRIVATE ${_opts})
    target_include_directories(${_t} PRIVATE
      "${LC_GEN}" "${LC_GEN}/EntitiesMP" "${LC_GEN}/EntitiesMP/StdH" "${LC_CLIENT_SRC}" "${LC_CLIENT_SRC}/${project}"
      "${LC_CLIENT_SRC}/EntitiesMP/StdH" "${LC_CLIENT_SRC}/include" "${LC_CLIENT_SRC}/Compat/d3dx8")
    target_include_directories(${_t} SYSTEM PRIVATE "${LC_CLIENT_SRC}/Compat/checkonly")
    target_link_libraries(${_t} PRIVATE Boost::headers SDL3::Headers)
  endforeach()
endif()
