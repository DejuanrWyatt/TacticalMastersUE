@echo off
REM Builds the class lab: the rules on their own, with no engine, answering the
REM class creator's two questions about a class file -- is it legal, and how
REM strong is it. See Tools\ClassLab\ClassLab.cpp.
REM
REM Run from anywhere. Output: Binaries\ClassLab\TMClassLab.exe. The class creator runs it from
REM there. Rebuild after any change to the rules, or the creator measures
REM classes against old ones.
setlocal
call "E:\VS2022\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
where cl >nul 2>&1
if errorlevel 1 (echo Could not find the MSVC compiler. Is Visual Studio installed? & exit /b 1)

set ROOT=%~dp0..\..
set PUB=%ROOT%\Source\TMSim\Public
set PRIV=%ROOT%\Source\TMSim\Private
set OUT=%ROOT%\Binaries\ClassLab
if not exist "%OUT%\obj" mkdir "%OUT%\obj"

cl /nologo /O2 /EHsc /std:c++17 /W4 /D_CRT_SECURE_NO_WARNINGS /I"%PUB%" "%ROOT%\Tools\ClassLab\ClassLab.cpp" "%PRIV%\SimTypes.cpp" "%PRIV%\SimUnit.cpp" "%PRIV%\SimAbility.cpp" "%PRIV%\SimMap.cpp" "%PRIV%\SimMovement.cpp" "%PRIV%\SimWorld.cpp" "%PRIV%\SimTargeting.cpp" "%PRIV%\SimResolve.cpp" "%PRIV%\SimAI.cpp" "%PRIV%\SimBattle.cpp" "%PRIV%\SimJson.cpp" "%PRIV%\SimClassFile.cpp" "%PRIV%\SimOrderText.cpp" /Fo:"%OUT%\obj\\" /Fe:"%OUT%\TMClassLab.exe"
if errorlevel 1 (echo CLASS LAB BUILD FAILED & exit /b 1)
echo Built %OUT%\TMClassLab.exe
exit /b 0
