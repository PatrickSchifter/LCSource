# --- Entity Class Compiler: turns *.es into C++ (classes, declarations and tables) ---

find_package(BISON REQUIRED)
find_package(FLEX REQUIRED)

# win_bison (WinFlexBison) is not safe to run twice at once: two grammars generated in parallel
# came out with their outputs mixed up. Every flex and bison run goes through one ninja job pool.
set_property(GLOBAL APPEND PROPERTY JOB_POOLS lc_flex_bison=1)

# lc_bison(<grammar.y> <output.cpp> <output.h> [bison options...])
function(lc_bison grammar output header)
  get_filename_component(dir "${output}" DIRECTORY)
  add_custom_command(
    OUTPUT "${output}" "${header}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${dir}"
    COMMAND "${BISON_EXECUTABLE}" ${ARGN} "--defines=${header}" -o "${output}" "${LC_CLIENT_SRC}/${grammar}"
    DEPENDS "${LC_CLIENT_SRC}/${grammar}"
    COMMENT "bison ${grammar}"
    JOB_POOL lc_flex_bison
    VERBATIM)
endfunction()

# lc_flex(<scanner.l> <output.cpp> <parser header it includes>)
function(lc_flex scanner output header)
  get_filename_component(dir "${output}" DIRECTORY)
  add_custom_command(
    OUTPUT "${output}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${dir}"
    COMMAND "${FLEX_EXECUTABLE}" "-o${output}" "${LC_CLIENT_SRC}/${scanner}"
    DEPENDS "${LC_CLIENT_SRC}/${scanner}"
    COMMENT "flex ${scanner}"
    JOB_POOL lc_flex_bison
    VERBATIM)
  set_property(SOURCE "${output}" APPEND PROPERTY OBJECT_DEPENDS "${header}")
endfunction()

lc_bison(Ecc/Parser.y "${LC_GEN}/Ecc/Parser.cpp" "${LC_GEN}/Ecc/Parser.h")
lc_flex(Ecc/Scanner.l "${LC_GEN}/Ecc/Scanner.cpp" "${LC_GEN}/Ecc/Parser.h")
add_executable(ecc Ecc/Main.cpp "${LC_GEN}/Ecc/Parser.cpp" "${LC_GEN}/Ecc/Scanner.cpp")
target_include_directories(ecc PRIVATE "${LC_GEN}" "${LC_CLIENT_SRC}")
target_compile_definitions(ecc PRIVATE _CRT_SECURE_NO_WARNINGS YY_NO_UNISTD_H)
if(NOT WIN32)
  target_compile_definitions(ecc PRIVATE PLATFORM_UNIX)
endif()
if(MSVC)
  target_compile_options(ecc PRIVATE /W0)
  # The parser stack (YYINITDEPTH) lives on the C stack; Windows defaults to 1 MB.
  target_link_options(ecc PRIVATE /STACK:16777216)
endif()

# lc_generate_entities(<out-var> <project-dir> <es...> [LINE])
# Copies each .es into the build tree and runs ecc there, so generated headers resolve as
# "<project>/<name>.h" through ${LC_GEN} without writing into the source tree.
function(lc_generate_entities out_var project_dir)
  cmake_parse_arguments(ARG "LINE" "" "" ${ARGN})
  set(generated)
  foreach(es IN LISTS ARG_UNPARSED_ARGUMENTS)
    file(RELATIVE_PATH rel "${LC_CLIENT_SRC}/${project_dir}" "${LC_CLIENT_SRC}/${es}")
    string(REGEX REPLACE "\\.es$" "" stem "${rel}")
    set(work "${LC_GEN}/${project_dir}")
    set(outputs "${work}/${stem}.cpp" "${work}/${stem}.h" "${work}/${stem}_tables.h")
    set(flags)
    if(ARG_LINE)
      set(flags -line)
    endif()
    add_custom_command(
      OUTPUT ${outputs}
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LC_CLIENT_SRC}/${es}" "${work}/${rel}"
      COMMAND ecc "${rel}" ${flags}
      WORKING_DIRECTORY "${work}"
      DEPENDS ecc "${LC_CLIENT_SRC}/${es}"
      COMMENT "ecc ${es}"
      VERBATIM)
    list(APPEND generated "${work}/${stem}.cpp")
  endforeach()
  set(${out_var} ${generated} PARENT_SCOPE)
endfunction()
