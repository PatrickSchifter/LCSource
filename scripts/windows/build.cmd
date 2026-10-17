@echo off
rem Same code page for configure and build: ninja matches cls localized /showIncludes prefix byte for byte
chcp 65001 >nul
rem Builds the Last Chaos client (x86). Usage: build.cmd [configure] [ninja targets...]
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul
set PATH=C:\Program Files\CMake\bin;%LOCALAPPDATA%\Microsoft\WinGet\Links;%PATH%
if "%1"=="configure" (
  cmake -S C:\LCSource -B C:\LCSource-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DLC_BUILD_SERVER=OFF -DLC_BUILD_CLIENT=ON -DLC_CLIENT_COUNTRY=BRAZIL ^
    -DLC_D3DX9_DIR=C:/LCSource-deps/d3dx/build/native -DLC_D3D8_INCLUDE_DIR=C:/LCSource-deps/d3d8/include -DBoost_DIR=C:/LCSource-deps/boost_1_86_0/stage32/lib/cmake/Boost-1.86.0 ^
    -DBISON_EXECUTABLE=C:\Users\schif\AppData\Local\Microsoft\WinGet\Packages\WinFlexBison.win_flex_bison_Microsoft.Winget.Source_8wekyb3d8bbwe\win_bison.exe -DFLEX_EXECUTABLE=C:\Users\schif\AppData\Local\Microsoft\WinGet\Packages\WinFlexBison.win_flex_bison_Microsoft.Winget.Source_8wekyb3d8bbwe\win_flex.exe > C:\LCSource-build-configure.log 2>&1
  type C:\LCSource-build-configure.log
  shift
)
ninja -C C:\LCSource-build -k 0 %* > C:\LCSource-build.log 2>&1
echo ninja exit %ERRORLEVEL%






