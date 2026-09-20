@echo off
rem Build libsam tools with MSVC. Usage: build.bat [x86|x64]
set ARCH=%1
if "%ARCH%"=="" set ARCH=x64
set PATH=C:\Program Files (x86)\Microsoft Visual Studio\Installer;%PATH%
call "D:\VisualStudio\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" %ARCH% >nul
cd /d %~dp0
if not exist ..\build\%ARCH% mkdir ..\build\%ARCH%
set CF=/nologo /O2 /W4 /fp:precise /D_CRT_SECURE_NO_WARNINGS /Fo..\build\%ARCH%\
cl %CF% sam.c samsynth.c /Fe:..\build\%ARCH%\samsynth.exe || exit /b 1
cl %CF% sam_lex.c sam_morph.c lextest.c /Fe:..\build\%ARCH%\lextest.exe || exit /b 1
if exist sam_front.c cl %CF% sam.c sam_lex.c sam_morph.c sam_pos.c sam_norm.c sam_front.c sam4fx.c sam_say.c /Fe:..\build\%ARCH%\sam_say.exe || exit /b 1
set LIB_SRC=sam.c sam_lex.c sam_morph.c sam_pos.c sam_norm.c sam_front.c sam4fx.c sam_tts.c
if exist sam_tts.c cl %CF% sam_cli.c %LIB_SRC% /Fe:..\build\%ARCH%\sam.exe || exit /b 1
if exist sam_tts.c cl %CF% /LD /DSAM_BUILD_DLL %LIB_SRC% /Fe:..\build\%ARCH%\sam.dll || exit /b 1
if exist ..\tests\lib\lib_test.c cl %CF% ..\tests\lib\lib_test.c %LIB_SRC% /Fe:..\build\%ARCH%\lib_test.exe || exit /b 1
rem standalone build with the voice data compiled in (run tools\embed_data.py first)
if exist sam_data.c cl %CF% /DSAM_EMBEDDED sam.c sam_lex.c sam_morph.c sam_pos.c sam_norm.c sam_front.c sam4fx.c sam_say.c sam_data.c /Fe:..\build\%ARCH%\sam_standalone.exe || exit /b 1
