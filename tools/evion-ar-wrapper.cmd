@echo off
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0evion-ar-wrapper.ps1" %*
exit /b %ERRORLEVEL%
