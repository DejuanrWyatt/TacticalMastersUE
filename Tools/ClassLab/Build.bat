@echo off
REM Builds the class lab: the rules on their own, with no engine, answering the
REM class creator's two questions about a class file -- is it legal, and how
REM strong is it. See Tools\ClassLab\ClassLab.cpp.
REM
REM Run from anywhere. Output: Binaries\ClassLab\TMClassLab.exe. The class creator runs it from
REM there. Rebuild after any change to the rules, or the creator measures
REM classes against old ones.
REM 2026-10-01: every rules file (Sim*.cpp), not a list -- the list had fallen behind
REM (camps, items, statuses), so the lab would not link.
REM 2026-10-02: and Cast Studio's looks (Source\TMCast), for "looks": today's look
REM of a class's abilities as events, which the creator imports.
setlocal
call "E:\VS2022\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
where cl >nul 2>&1
if errorlevel 1 (echo Could not find the MSVC compiler. Is Visual Studio installed? & exit /b 1)

set ROOT=%~dp0..\..
set PUB=%ROOT%\Source\TMSim\Public
set PRIV=%ROOT%\Source\TMSim\Private
set CASTPUB=%ROOT%\Source\TMCast\Public
set CASTPRIV=%ROOT%\Source\TMCast\Private
set OUT=%ROOT%\Binaries\ClassLab
if not exist "%OUT%\obj" mkdir "%OUT%\obj"

cl /nologo /O2 /EHsc /std:c++17 /W4 /D_CRT_SECURE_NO_WARNINGS /I"%PUB%" /I"%CASTPUB%" "%ROOT%\Tools\ClassLab\ClassLab.cpp" "%PRIV%\Sim*.cpp" "%CASTPRIV%\CastLooks.cpp" "%CASTPRIV%\CastLegacy.cpp" /Fo:"%OUT%\obj\\" /Fe:"%OUT%\TMClassLab.exe"
if errorlevel 1 (echo CLASS LAB BUILD FAILED & exit /b 1)
echo Built %OUT%\TMClassLab.exe
exit /b 0
