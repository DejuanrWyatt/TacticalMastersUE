@echo off
REM Builds the map analyser: the rules on their own, with no engine, measuring what
REM a battle map will play like. See Tools\MapAnalyzer\MapAnalyzer.cpp.
REM
REM Run from anywhere. Output: Binaries\MapAnalyzer\TMMapAnalyzer.exe. Rebuild after
REM any change to the rules, or the maps are measured against old ones.
REM scripts\map-analyze.bat builds and runs it in one go.
setlocal
call "E:\VS2022\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
where cl >nul 2>&1
if errorlevel 1 (echo Could not find the MSVC compiler. Is Visual Studio installed? & exit /b 1)

set ROOT=%~dp0..\..
set PUB=%ROOT%\Source\TMSim\Public
set PRIV=%ROOT%\Source\TMSim\Private
set OUT=%ROOT%\Binaries\MapAnalyzer
if not exist "%OUT%\obj" mkdir "%OUT%\obj"

cl /nologo /O2 /EHsc /std:c++17 /W4 /D_CRT_SECURE_NO_WARNINGS /I"%PUB%" "%ROOT%\Tools\MapAnalyzer\MapAnalyzer.cpp" "%PRIV%\SimTypes.cpp" "%PRIV%\SimUnit.cpp" "%PRIV%\SimAbility.cpp" "%PRIV%\SimMap.cpp" "%PRIV%\SimMovement.cpp" "%PRIV%\SimWorld.cpp" "%PRIV%\SimTargeting.cpp" "%PRIV%\SimResolve.cpp" "%PRIV%\SimAI.cpp" "%PRIV%\SimBattle.cpp" "%PRIV%\SimJson.cpp" "%PRIV%\SimClassFile.cpp" "%PRIV%\SimOrderText.cpp" /Fo:"%OUT%\obj\\" /Fe:"%OUT%\TMMapAnalyzer.exe" >"%OUT%\build.log" 2>&1
if errorlevel 1 (type "%OUT%\build.log" & echo MAP ANALYSER BUILD FAILED & exit /b 1)
findstr /c:"MapAnalyzer.cpp(" "%OUT%\build.log" | findstr /c:"warning"
exit /b 0
