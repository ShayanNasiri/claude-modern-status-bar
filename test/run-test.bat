@echo off
REM Smoke test: pipe mock.json into statusline.exe and print the rendered line.
"%~dp0..\statusline.exe" < "%~dp0mock.json"
echo.
