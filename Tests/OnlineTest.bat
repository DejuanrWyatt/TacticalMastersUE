@echo off
REM Two copies of the game on this machine play each other online, headless:
REM they must finish the same battle, and a game made to differ must be caught.
REM Close the editor first. About five minutes. See Tests\OnlineTest.ps1.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0OnlineTest.ps1"
exit /b %ERRORLEVEL%
