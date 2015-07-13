@echo off
setlocal

set gccinps=test.c ltjson.c
set gccopts=-Wall -mconsole
set gccdefs=-DWINVER=0x0500 -D_WIN32_WINNT=0x500
set gcclibs=
set gccexec=test.exe

echo Compiling %gccexec%

@echo on
gcc %gccdefs% %gccopts% -o %gccexec% %gccinps% %gcclibs%
