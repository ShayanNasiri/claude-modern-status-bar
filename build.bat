@echo off
REM Build statusline.exe with MSVC.
REM   /MT  static-link CRT (no DLL load at startup = faster cold start)
REM   /O2  optimize for speed
REM Output lands next to this script as statusline.exe.
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 (echo vcvars FAILED - is MSVC 2022 installed? & exit /b 1)
pushd "%~dp0"
cl /nologo /O2 /MT /W3 /D_CRT_SECURE_NO_WARNINGS /Fe:statusline.exe statusline.c /link /SUBSYSTEM:CONSOLE
set rc=%errorlevel%
del statusline.obj 2>nul
popd
exit /b %rc%
