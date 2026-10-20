# --- Engine parsers, generated from their grammars: the shell's script language (Base) and the
# SKA model instance scripts (.smc, Ska). Uses lc_bison/lc_flex from cmake/Ecc.cmake. ---
#
# ENGINE_PARSER_SOURCES: the generated .cpp files, to compile into the Engine.

set(_base "${LC_GEN}/Engine/Base")
set(_ska "${LC_GEN}/Engine/Ska")
lc_bison(Engine/Base/Parser.y "${_base}/Parser.cpp" "${_base}/Parser.h")
lc_flex(Engine/Base/Scanner.l "${_base}/Scanner.cpp" "${_base}/Parser.h")
lc_bison(Engine/Ska/smcPars.y "${_ska}/smcPars.cpp" "${_ska}/smcPars.h" -p syy)
lc_flex(Engine/Ska/smcScan.l "${_ska}/smcScan.cpp" "${_ska}/smcPars.h")

set(ENGINE_PARSER_SOURCES
  "${_base}/Parser.cpp" "${_base}/Scanner.cpp" "${_ska}/smcPars.cpp" "${_ska}/smcScan.cpp")
