@echo off
rem Portability check: build libsam + sam_say with clang in strict C99 mode (clang must be on PATH,
rem e.g. the "C++ Clang tools" Visual Studio component or LLVM for Windows).
cd /d %~dp0
if not exist ..\build\clang mkdir ..\build\clang
clang -std=c99 -pedantic -Wall -Wextra -Wno-unused-parameter -O2 -D_CRT_SECURE_NO_WARNINGS sam.c sam_lex.c sam_morph.c sam_pos.c sam_norm.c sam_front.c sam_say.c -o ..\build\clang\sam_say.exe
