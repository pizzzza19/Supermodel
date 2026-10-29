@echo off
setlocal
cd /d "%~dp0.."
call "%ProgramFiles%\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b 1
set "OBJ=MSVC\x64\Release"
cl /nologo /std:c++17 /EHsc /MT /O2 /DSUPERMODEL_WIN32 /I Src /I Src\OSD /I Src\OSD\SDL /I Src\Pkgs /I Libraries\SDL2-2.30.11\include /Fo"%OBJ%\Test_LauncherScale.obj" /Fe"%OBJ%\Test_LauncherScale.exe" Scripts\Test_LauncherScale.cpp ^
 "%OBJ%\imgui.obj" "%OBJ%\imgui_draw.obj" "%OBJ%\imgui_tables.obj" "%OBJ%\imgui_widgets.obj" "%OBJ%\imgui_impl_sdlrenderer2.obj" ^
 /link /LTCG /LIBPATH:"%OBJ%" SDL2.lib
exit /b %errorlevel%
