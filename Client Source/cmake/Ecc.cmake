# --- Entity Class Compiler: turns *.es into C++ (classes, declarations and tables) ---

find_package(BISON REQUIRED)
find_package(FLEX REQUIRED)
BISON_TARGET(EccParser Ecc/Parser.y "${LC_GEN}/Ecc/Parser.cpp" DEFINES_FILE "${LC_GEN}/Ecc/Parser.h")
FLEX_TARGET(EccScanner Ecc/Scanner.l "${LC_GEN}/Ecc/Scanner.cpp")
ADD_FLEX_BISON_DEPENDENCY(EccScanner EccParser)
add_executable(ecc Ecc/Main.cpp ${BISON_EccParser_OUTPUTS} ${FLEX_EccScanner_OUTPUTS})
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
