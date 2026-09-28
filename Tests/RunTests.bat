@echo off
REM Builds and runs the plain-C++ tests for the battle rules. No Unreal, no
REM editor: the sim is deliberately engine-free so it can be checked in seconds.
REM
REM The tests measure the port against the Godot game rather than against
REM themselves. The battle is a deterministic simulation -- an online match
REM steps the same rules on both machines and compares checksums, and a replay
REM re-runs a recorded fight -- so "close enough" is not a thing it can be.
setlocal
call "E:\VS2022\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
REM vcvars leaves a non-zero errorlevel over a harmless vswhere warning, so ask
REM whether the compiler is actually on the path rather than trusting that.
where cl >nul 2>&1
if errorlevel 1 (echo Could not find the MSVC compiler. Is Visual Studio installed? & exit /b 1)

set HERE=%~dp0
set PUB=%HERE%..\Source\TMSim\Public
set PRIV=%HERE%..\Source\TMSim\Private
set OUT=%TEMP%\tmsim_tests
if not exist "%OUT%" mkdir "%OUT%"
if not exist "%OUT%\obj" mkdir "%OUT%\obj"
set FAILED=0

echo === the dice ===
cl /nologo /EHsc /std:c++17 /W4 /D_CRT_SECURE_NO_WARNINGS /I"%PUB%" "%HERE%SimRandomTest.cpp" /Fo:"%OUT%\obj\\" /Fe:"%OUT%\SimRandomTest.exe" >nul
if errorlevel 1 (echo BUILD FAILED & exit /b 1)
"%OUT%\SimRandomTest.exe" || set FAILED=1

echo.
echo === the clock ===
cl /nologo /EHsc /std:c++17 /W4 /D_CRT_SECURE_NO_WARNINGS /I"%PUB%" "%HERE%SimTickTest.cpp" "%PRIV%\SimTypes.cpp" "%PRIV%\SimUnit.cpp" "%PRIV%\SimAbility.cpp" "%PRIV%\SimMap.cpp" "%PRIV%\SimMovement.cpp" "%PRIV%\SimWorld.cpp" "%PRIV%\SimTargeting.cpp" "%PRIV%\SimResolve.cpp" "%PRIV%\SimAI.cpp" "%PRIV%\SimBattle.cpp" "%PRIV%\SimJson.cpp" "%PRIV%\SimClassFile.cpp" /Fo:"%OUT%\obj\\" /Fe:"%OUT%\SimTickTest.exe" >nul
if errorlevel 1 (echo BUILD FAILED & exit /b 1)
"%OUT%\SimTickTest.exe" "%HERE%GodotTickTrace.txt" || set FAILED=1

echo.
echo === what an ability does ===
cl /nologo /EHsc /std:c++17 /W4 /D_CRT_SECURE_NO_WARNINGS /I"%PUB%" "%HERE%SimCalcTest.cpp" "%PRIV%\SimTypes.cpp" "%PRIV%\SimUnit.cpp" "%PRIV%\SimAbility.cpp" "%PRIV%\SimMap.cpp" "%PRIV%\SimMovement.cpp" "%PRIV%\SimWorld.cpp" "%PRIV%\SimTargeting.cpp" "%PRIV%\SimResolve.cpp" "%PRIV%\SimAI.cpp" "%PRIV%\SimBattle.cpp" "%PRIV%\SimJson.cpp" "%PRIV%\SimClassFile.cpp" /Fo:"%OUT%\obj\\" /Fe:"%OUT%\SimCalcTest.exe" >nul
if errorlevel 1 (echo BUILD FAILED & exit /b 1)
"%OUT%\SimCalcTest.exe" "%HERE%GodotCalcTable.txt" || set FAILED=1

echo.
echo === walking ===
cl /nologo /EHsc /std:c++17 /W4 /D_CRT_SECURE_NO_WARNINGS /I"%PUB%" "%HERE%SimMoveTest.cpp" "%PRIV%\SimTypes.cpp" "%PRIV%\SimUnit.cpp" "%PRIV%\SimAbility.cpp" "%PRIV%\SimMap.cpp" "%PRIV%\SimMovement.cpp" "%PRIV%\SimWorld.cpp" "%PRIV%\SimTargeting.cpp" "%PRIV%\SimResolve.cpp" "%PRIV%\SimAI.cpp" "%PRIV%\SimBattle.cpp" "%PRIV%\SimJson.cpp" "%PRIV%\SimClassFile.cpp" /Fo:"%OUT%\obj\\" /Fe:"%OUT%\SimMoveTest.exe" >nul
if errorlevel 1 (echo BUILD FAILED & exit /b 1)
"%OUT%\SimMoveTest.exe" "%HERE%GodotMoveTable.txt" || set FAILED=1

echo.
echo === the computer player ===
cl /nologo /EHsc /std:c++17 /W4 /D_CRT_SECURE_NO_WARNINGS /I"%PUB%" "%HERE%SimAITest.cpp" "%PRIV%\SimTypes.cpp" "%PRIV%\SimUnit.cpp" "%PRIV%\SimAbility.cpp" "%PRIV%\SimMap.cpp" "%PRIV%\SimMovement.cpp" "%PRIV%\SimWorld.cpp" "%PRIV%\SimTargeting.cpp" "%PRIV%\SimResolve.cpp" "%PRIV%\SimAI.cpp" "%PRIV%\SimBattle.cpp" "%PRIV%\SimJson.cpp" "%PRIV%\SimClassFile.cpp" /Fo:"%OUT%\obj\\" /Fe:"%OUT%\SimAITest.exe" >nul
if errorlevel 1 (echo BUILD FAILED & exit /b 1)
"%OUT%\SimAITest.exe" "%HERE%GodotAITable.txt" || set FAILED=1

echo.
echo === what an ability does when it goes off ===
cl /nologo /EHsc /std:c++17 /W4 /D_CRT_SECURE_NO_WARNINGS /I"%PUB%" "%HERE%SimAbilityTest.cpp" "%PRIV%\SimTypes.cpp" "%PRIV%\SimUnit.cpp" "%PRIV%\SimAbility.cpp" "%PRIV%\SimMap.cpp" "%PRIV%\SimMovement.cpp" "%PRIV%\SimWorld.cpp" "%PRIV%\SimTargeting.cpp" "%PRIV%\SimResolve.cpp" "%PRIV%\SimAI.cpp" "%PRIV%\SimBattle.cpp" "%PRIV%\SimJson.cpp" "%PRIV%\SimClassFile.cpp" /Fo:"%OUT%\obj\\" /Fe:"%OUT%\SimAbilityTest.exe" >nul
if errorlevel 1 (echo BUILD FAILED & exit /b 1)
"%OUT%\SimAbilityTest.exe" "%HERE%GodotAbilityTable.txt" || set FAILED=1
echo.
echo === what the computer does with a turn ===
cl /nologo /EHsc /std:c++17 /W4 /D_CRT_SECURE_NO_WARNINGS /I"%PUB%" "%HERE%SimAIActionTest.cpp" "%PRIV%\SimTypes.cpp" "%PRIV%\SimUnit.cpp" "%PRIV%\SimAbility.cpp" "%PRIV%\SimMap.cpp" "%PRIV%\SimMovement.cpp" "%PRIV%\SimWorld.cpp" "%PRIV%\SimTargeting.cpp" "%PRIV%\SimResolve.cpp" "%PRIV%\SimAI.cpp" "%PRIV%\SimBattle.cpp" "%PRIV%\SimJson.cpp" "%PRIV%\SimClassFile.cpp" /Fo:"%OUT%\obj\\" /Fe:"%OUT%\SimAIActionTest.exe" >nul
if errorlevel 1 (echo BUILD FAILED & exit /b 1)
"%OUT%\SimAIActionTest.exe" "%HERE%GodotAIActionTable.txt" || set FAILED=1
echo.
echo === a battle played and replayed ===
cl /nologo /EHsc /std:c++17 /W4 /D_CRT_SECURE_NO_WARNINGS /I"%PUB%" "%HERE%SimPlayTest.cpp" "%PRIV%\SimTypes.cpp" "%PRIV%\SimUnit.cpp" "%PRIV%\SimAbility.cpp" "%PRIV%\SimMap.cpp" "%PRIV%\SimMovement.cpp" "%PRIV%\SimWorld.cpp" "%PRIV%\SimTargeting.cpp" "%PRIV%\SimResolve.cpp" "%PRIV%\SimAI.cpp" "%PRIV%\SimBattle.cpp" "%PRIV%\SimJson.cpp" "%PRIV%\SimClassFile.cpp" /Fo:"%OUT%\obj\\" /Fe:"%OUT%\SimPlayTest.exe" >nul
if errorlevel 1 (echo BUILD FAILED & exit /b 1)
"%OUT%\SimPlayTest.exe" || set FAILED=1

echo.
echo === the class files ===
cl /nologo /EHsc /std:c++17 /W4 /D_CRT_SECURE_NO_WARNINGS /I"%PUB%" "%HERE%SimClassTest.cpp" "%PRIV%\SimTypes.cpp" "%PRIV%\SimUnit.cpp" "%PRIV%\SimAbility.cpp" "%PRIV%\SimMap.cpp" "%PRIV%\SimMovement.cpp" "%PRIV%\SimWorld.cpp" "%PRIV%\SimTargeting.cpp" "%PRIV%\SimResolve.cpp" "%PRIV%\SimAI.cpp" "%PRIV%\SimBattle.cpp" "%PRIV%\SimJson.cpp" "%PRIV%\SimClassFile.cpp" /Fo:"%OUT%\obj\\" /Fe:"%OUT%\SimClassTest.exe" >nul
if errorlevel 1 (echo BUILD FAILED & exit /b 1)
"%OUT%\SimClassTest.exe" "%HERE%..\Content\Data\Classes" "%HERE%GodotClassTable.txt" || set FAILED=1

echo.
echo === whole battles, replayed from Godot's orders ===
cl /nologo /EHsc /std:c++17 /W4 /D_CRT_SECURE_NO_WARNINGS /I"%PUB%" "%HERE%SimTraceTest.cpp" "%PRIV%\SimTypes.cpp" "%PRIV%\SimUnit.cpp" "%PRIV%\SimAbility.cpp" "%PRIV%\SimMap.cpp" "%PRIV%\SimMovement.cpp" "%PRIV%\SimWorld.cpp" "%PRIV%\SimTargeting.cpp" "%PRIV%\SimResolve.cpp" "%PRIV%\SimAI.cpp" "%PRIV%\SimBattle.cpp" "%PRIV%\SimJson.cpp" "%PRIV%\SimClassFile.cpp" /Fo:"%OUT%\obj\\" /Fe:"%OUT%\SimTraceTest.exe" >nul
if errorlevel 1 (echo BUILD FAILED & exit /b 1)
"%OUT%\SimTraceTest.exe" "%HERE%..\Content\Data\Classes" "%HERE%GodotBattleTrace.txt" || set FAILED=1

echo.
if "%FAILED%"=="1" (echo SOME TESTS FAILED & exit /b 1)
echo ALL TESTS PASSED
exit /b 0