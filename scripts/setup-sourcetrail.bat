@echo off
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup-sourcetrail.ps1" %*
exit /b %ERRORLEVEL%
