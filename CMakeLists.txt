cmake_minimum_required(VERSION 3.24)
project(LastChaos LANGUAGES C CXX)

option(LC_BUILD_SERVER "Build the server programs" ON)
option(LC_BUILD_CLIENT "Build the game client (32-bit Windows/MSVC, or macOS)" OFF)

# Region the code is compiled for. The original Makefiles passed -DLC_<REGION>=<id>.
set(LC_REGION "USA" CACHE STRING "Target region (KOR, TWN, TLD, JPN, MAL, USA, BRZ, HBK, GER, SPN, FRC, PLD, RUS, TUR, ITA, MEX, ESP_USA, FRA_USA, NLD, UK)")
set(_lc_region_ids KOR=0 TWN=1 CHN=3 TLD=4 JPN=6 MAL=7 USA=9 BRZ=10 HBK=11 GER=13 SPN=14
                   FRC=15 PLD=16 RUS=17 TUR=18 ITA=19 MEX=20 ESP_USA=21 FRA_USA=22 NLD=23 UK=24)
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
message(STATUS "Last Chaos region: ${LC_REGION_DEFINE}")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(LC_BUILD_SERVER)
  add_subdirectory("Server Source")
endif()

if(LC_BUILD_CLIENT)
  add_subdirectory("Client Source")
endif()
