@echo off
REM Builds and runs the plain-C++ tests for the battle rules. No Unreal, no
REM editor: the sim is deliberately engine-free so it can be checked in seconds.
REM
REM The RNG test is the important one. The battle is a deterministic simulation
REM and that is only worth anything if every roll matches Godot exactly, so the
REM numbers it checks are Godot's own output. If this stops passing, a replay or
REM an online match will drift and it will be far harder to work out why.
setlocal
call "E:\VS2022\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
REM vcvars leaves a non-zero errorlevel over a harmless vswhere warning, so ask
REM whether the compiler is actually on the path rather than trusting that.
where cl >nul 2>&1
if errorlevel 1 (echo Could not find the MSVC compiler. Is Visual Studio installed? & exit /b 1)
set OUT=%TEMP%\tmsim_tests
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /EHsc /std:c++17 /W4 /I"%~dp0..\Public" "%~dp0SimRandomTest.cpp" /Fo:"%OUT%\\" /Fe:"%OUT%\SimRandomTest.exe" >nul
if errorlevel 1 (echo BUILD FAILED & exit /b 1)
"%OUT%\SimRandomTest.exe"
exit /b %errorlevel%
