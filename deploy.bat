@echo off
REM Build and install statusline.exe into %USERPROFILE%\.claude\
REM where Claude Code expects it (per settings.json statusLine.command).
setlocal
call "%~dp0build.bat"
if errorlevel 1 (echo build FAILED & exit /b 1)
copy /Y "%~dp0statusline.exe" "%USERPROFILE%\.claude\statusline.exe" >nul
if errorlevel 1 (echo copy FAILED & exit /b 1)
echo deployed: %USERPROFILE%\.claude\statusline.exe
endlocal
exit /b 0
