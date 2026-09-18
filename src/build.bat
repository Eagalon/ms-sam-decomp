@echo off
rem Build with MSVC. Usage: build.bat [x86|x64]  (run from any prompt; finds Visual Studio via vswhere)
set ARCH=%1
if "%ARCH%"=="" set ARCH=x64
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not defined VCINSTALLDIR call :findvs
cd /d %~dp0
if not exist ..\build\%ARCH% mkdir ..\build\%ARCH%
set CF=/nologo /O2 /W4 /fp:precise /D_CRT_SECURE_NO_WARNINGS /Fo..\build\%ARCH%\
cl %CF% sam.c samsynth.c /Fe:..\build\%ARCH%\samsynth.exe || exit /b 1
cl %CF% sam_lex.c sam_morph.c lextest.c /Fe:..\build\%ARCH%\lextest.exe || exit /b 1
if exist sam_front.c cl %CF% sam.c sam_lex.c sam_morph.c sam_pos.c sam_norm.c sam_front.c sam4fx.c sam_say.c /Fe:..\build\%ARCH%\sam_say.exe || exit /b 1
rem standalone build with the voice data compiled in (run tools\embed_data.py first)
if exist sam_data.c cl %CF% /DSAM_EMBEDDED sam.c sam_lex.c sam_morph.c sam_pos.c sam_norm.c sam_front.c sam4fx.c sam_say.c sam_data.c /Fe:..\build\%ARCH%\sam_standalone.exe || exit /b 1
goto :eof

:findvs
for %%d in ("%VSWHERE%") do set "PATH=%%~dpd;%PATH%"
for /f "usebackq delims=" %%i in (`call "%VSWHERE%" -latest -products * -property installationPath`) do call "%%i\VC\Auxiliary\Build\vcvarsall.bat" %ARCH% >nul
goto :eof
