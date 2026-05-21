@echo off
REM ===========================================================================
REM PitchCorrectorVST - one-click build + copy + launch wrapper
REM Double-click this from Explorer, or run from any cmd prompt.
REM ===========================================================================
setlocal
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File ".\build-and-launch.ps1" %*
exit /b %ERRORLEVEL%
