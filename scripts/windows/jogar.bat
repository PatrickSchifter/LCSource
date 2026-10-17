@echo off
rem hw-shaders.txt present: hardware vertex/pixel shaders (skinning on the GPU) and SKA render log
cd /d C:\LC-BR2014
if exist hw-shaders.txt (set LC_DEBUG_SKA=1) else (set LC_NO_HW_SHADERS=1)
start "" Bin\Nksp.exe fkzktlfgod!
