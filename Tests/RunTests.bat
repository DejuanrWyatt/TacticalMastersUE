@echo off
REM Builds and runs the plain-C++ tests for the battle rules. No Unreal, no
REM editor: the sim is deliberately engine-free so it can be checked in seconds.
REM
REM The rules are held to what they did when each baseline in Tests\Baselines
REM was recorded: a battle's clock, what every ability does, walking, the
REM computer player's choices, whole battles. The battle is a deterministic
REM simulation -- an online match steps the same rules on both machines and
REM compares checksums, and a replay re-runs a recorded fight -- so "close
REM enough" is not a thing it can be.
REM
REM   RunTests.bat               check the rules against the baselines
REM   RunTests.bat --rebaseline  after changing a rule on purpose: write the
REM                              baselines again from the rules (Baseline.h),
REM                              then read the diff before committing it
setlocal
set REBASELINE=
if /i "%~1"=="--rebaseline" set REBASELINE=--rebaseline
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
cl /nologo /EHsc /std:c++17 /W4 /D_CRT_SECURE_NO_WARNINGS /I"%PUB%" "%HERE%SimTickTest.cpp" "%PRIV%\SimTypes.cpp" "%PRIV%\SimUnit.cpp" "%PRIV%\SimAbility.cpp" "%PRIV%\SimMap.cpp" "%PRIV%\SimMovement.cpp" "%PRIV%\SimWorld.cpp" "%PRIV%\SimTargeting.cpp" "%PRIV%\SimResolve.cpp" "%PRIV%\SimAI.cpp" "%PRIV%\SimBattle.cpp" "%PRIV%\SimJson.cpp" "%PRIV%\SimClassFile.cpp" "%PRIV%\SimOrderText.cpp" "%PRIV%\SimItem.cpp" "%PRIV%\SimCamps.cpp" "%PRIV%\SimNeutral.cpp" "%PRIV%\SimStatuses.cpp" /Fo:"%OUT%\obj\\" /Fe:"%OUT%\SimTickTest.exe" >nul
if errorlevel 1 (echo BUILD FAILED & exit /b 1)
"%OUT%\SimTickTest.exe" "%HERE%Baselines\TickTrace.txt" %REBASELINE% || set FAILED=1

echo.
echo === what an ability does ===
cl /nologo /EHsc /std:c++17 /W4 /D_CRT_SECURE_NO_WARNINGS /I"%PUB%" "%HERE%SimCalcTest.cpp" "%PRIV%\SimTypes.cpp" "%PRIV%\SimUnit.cpp" "%PRIV%\SimAbility.cpp" "%PRIV%\SimMap.cpp" "%PRIV%\SimMovement.cpp" "%PRIV%\SimWorld.cpp" "%PRIV%\SimTargeting.cpp" "%PRIV%\SimResolve.cpp" "%PRIV%\SimAI.cpp" "%PRIV%\SimBattle.cpp" "%PRIV%\SimJson.cpp" "%PRIV%\SimClassFile.cpp" "%PRIV%\SimOrderText.cpp" "%PRIV%\SimItem.cpp" "%PRIV%\SimCamps.cpp" "%PRIV%\SimNeutral.cpp" "%PRIV%\SimStatuses.cpp" /Fo:"%OUT%\obj\\" /Fe:"%OUT%\SimCalcTest.exe" >nul
if errorlevel 1 (echo BUILD FAILED & exit /b 1)
"%OUT%\SimCalcTest.exe" "%HERE%Baselines\CalcTable.txt" %REBASELINE% || set FAILED=1

echo.
echo === walking ===
cl /nologo /EHsc /std:c++17 /W4 /D_CRT_SECURE_NO_WARNINGS /I"%PUB%" "%HERE%SimMoveTest.cpp" "%PRIV%\SimTypes.cpp" "%PRIV%\SimUnit.cpp" "%PRIV%\SimAbility.cpp" "%PRIV%\SimMap.cpp" "%PRIV%\SimMovement.cpp" "%PRIV%\SimWorld.cpp" "%PRIV%\SimTargeting.cpp" "%PRIV%\SimResolve.cpp" "%PRIV%\SimAI.cpp" "%PRIV%\SimBattle.cpp" "%PRIV%\SimJson.cpp" "%PRIV%\SimClassFile.cpp" "%PRIV%\SimOrderText.cpp" "%PRIV%\SimItem.cpp" "%PRIV%\SimCamps.cpp" "%PRIV%\SimNeutral.cpp" "%PRIV%\SimStatuses.cpp" /Fo:"%OUT%\obj\\" /Fe:"%OUT%\SimMoveTest.exe" >nul
if errorlevel 1 (echo BUILD FAILED & exit /b 1)
"%OUT%\SimMoveTest.exe" "%HERE%Baselines\MoveTable.txt" %REBASELINE% || set FAILED=1

echo.
echo === the computer player ===
cl /nologo /EHsc /std:c++17 /W4 /D_CRT_SECURE_NO_WARNINGS /I"%PUB%" "%HERE%SimAITest.cpp" "%PRIV%\SimTypes.cpp" "%PRIV%\SimUnit.cpp" "%PRIV%\SimAbility.cpp" "%PRIV%\SimMap.cpp" "%PRIV%\SimMovement.cpp" "%PRIV%\SimWorld.cpp" "%PRIV%\SimTargeting.cpp" "%PRIV%\SimResolve.cpp" "%PRIV%\SimAI.cpp" "%PRIV%\SimBattle.cpp" "%PRIV%\SimJson.cpp" "%PRIV%\SimClassFile.cpp" "%PRIV%\SimOrderText.cpp" "%PRIV%\SimItem.cpp" "%PRIV%\SimCamps.cpp" "%PRIV%\SimNeutral.cpp" "%PRIV%\SimStatuses.cpp" /Fo:"%OUT%\obj\\" /Fe:"%OUT%\SimAITest.exe" >nul
if errorlevel 1 (echo BUILD FAILED & exit /b 1)
"%OUT%\SimAITest.exe" "%HERE%Baselines\AITable.txt" %REBASELINE% || set FAILED=1

echo.
echo === what an ability does when it goes off ===
cl /nologo /EHsc /std:c++17 /W4 /D_CRT_SECURE_NO_WARNINGS /I"%PUB%" "%HERE%SimAbilityTest.cpp" "%PRIV%\SimTypes.cpp" "%PRIV%\SimUnit.cpp" "%PRIV%\SimAbility.cpp" "%PRIV%\SimMap.cpp" "%PRIV%\SimMovement.cpp" "%PRIV%\SimWorld.cpp" "%PRIV%\SimTargeting.cpp" "%PRIV%\SimResolve.cpp" "%PRIV%\SimAI.cpp" "%PRIV%\SimBattle.cpp" "%PRIV%\SimJson.cpp" "%PRIV%\SimClassFile.cpp" "%PRIV%\SimOrderText.cpp" "%PRIV%\SimItem.cpp" "%PRIV%\SimCamps.cpp" "%PRIV%\SimNeutral.cpp" "%PRIV%\SimStatuses.cpp" /Fo:"%OUT%\obj\\" /Fe:"%OUT%\SimAbilityTest.exe" >nul
if errorlevel 1 (echo BUILD FAILED & exit /b 1)
"%OUT%\SimAbilityTest.exe" "%HERE%Baselines\AbilityTable.txt" %REBASELINE% || set FAILED=1
echo.
echo === what the computer does with a turn ===
cl /nologo /EHsc /std:c++17 /W4 /D_CRT_SECURE_NO_WARNINGS /I"%PUB%" "%HERE%SimAIActionTest.cpp" "%PRIV%\SimTypes.cpp" "%PRIV%\SimUnit.cpp" "%PRIV%\SimAbility.cpp" "%PRIV%\SimMap.cpp" "%PRIV%\SimMovement.cpp" "%PRIV%\SimWorld.cpp" "%PRIV%\SimTargeting.cpp" "%PRIV%\SimResolve.cpp" "%PRIV%\SimAI.cpp" "%PRIV%\SimBattle.cpp" "%PRIV%\SimJson.cpp" "%PRIV%\SimClassFile.cpp" "%PRIV%\SimOrderText.cpp" "%PRIV%\SimItem.cpp" "%PRIV%\SimCamps.cpp" "%PRIV%\SimNeutral.cpp" "%PRIV%\SimStatuses.cpp" /Fo:"%OUT%\obj\\" /Fe:"%OUT%\SimAIActionTest.exe" >nul
if errorlevel 1 (echo BUILD FAILED & exit /b 1)
"%OUT%\SimAIActionTest.exe" "%HERE%Baselines\AIActionTable.txt" %REBASELINE% || set FAILED=1
echo.
echo === a battle played and replayed ===
cl /nologo /EHsc /std:c++17 /W4 /D_CRT_SECURE_NO_WARNINGS /I"%PUB%" "%HERE%SimPlayTest.cpp" "%PRIV%\SimTypes.cpp" "%PRIV%\SimUnit.cpp" "%PRIV%\SimAbility.cpp" "%PRIV%\SimMap.cpp" "%PRIV%\SimMovement.cpp" "%PRIV%\SimWorld.cpp" "%PRIV%\SimTargeting.cpp" "%PRIV%\SimResolve.cpp" "%PRIV%\SimAI.cpp" "%PRIV%\SimBattle.cpp" "%PRIV%\SimJson.cpp" "%PRIV%\SimClassFile.cpp" "%PRIV%\SimOrderText.cpp" "%PRIV%\SimItem.cpp" "%PRIV%\SimCamps.cpp" "%PRIV%\SimNeutral.cpp" "%PRIV%\SimStatuses.cpp" /Fo:"%OUT%\obj\\" /Fe:"%OUT%\SimPlayTest.exe" >nul
if errorlevel 1 (echo BUILD FAILED & exit /b 1)
"%OUT%\SimPlayTest.exe" || set FAILED=1

echo.
echo === orders as text, between two machines ===
cl /nologo /EHsc /std:c++17 /W4 /D_CRT_SECURE_NO_WARNINGS /I"%PUB%" "%HERE%SimOrderTextTest.cpp" "%PRIV%\SimTypes.cpp" "%PRIV%\SimUnit.cpp" "%PRIV%\SimAbility.cpp" "%PRIV%\SimMap.cpp" "%PRIV%\SimMovement.cpp" "%PRIV%\SimWorld.cpp" "%PRIV%\SimTargeting.cpp" "%PRIV%\SimResolve.cpp" "%PRIV%\SimAI.cpp" "%PRIV%\SimBattle.cpp" "%PRIV%\SimJson.cpp" "%PRIV%\SimClassFile.cpp" "%PRIV%\SimOrderText.cpp" "%PRIV%\SimItem.cpp" "%PRIV%\SimCamps.cpp" "%PRIV%\SimNeutral.cpp" "%PRIV%\SimStatuses.cpp" /Fo:"%OUT%\obj\\" /Fe:"%OUT%\SimOrderTextTest.exe" >nul
if errorlevel 1 (echo BUILD FAILED & exit /b 1)
"%OUT%\SimOrderTextTest.exe" || set FAILED=1

echo.
echo === the class files ===
cl /nologo /EHsc /std:c++17 /W4 /D_CRT_SECURE_NO_WARNINGS /I"%PUB%" "%HERE%SimClassTest.cpp" "%PRIV%\SimTypes.cpp" "%PRIV%\SimUnit.cpp" "%PRIV%\SimAbility.cpp" "%PRIV%\SimMap.cpp" "%PRIV%\SimMovement.cpp" "%PRIV%\SimWorld.cpp" "%PRIV%\SimTargeting.cpp" "%PRIV%\SimResolve.cpp" "%PRIV%\SimAI.cpp" "%PRIV%\SimBattle.cpp" "%PRIV%\SimJson.cpp" "%PRIV%\SimClassFile.cpp" "%PRIV%\SimOrderText.cpp" "%PRIV%\SimItem.cpp" "%PRIV%\SimCamps.cpp" "%PRIV%\SimNeutral.cpp" "%PRIV%\SimStatuses.cpp" /Fo:"%OUT%\obj\\" /Fe:"%OUT%\SimClassTest.exe" >nul
if errorlevel 1 (echo BUILD FAILED & exit /b 1)
"%OUT%\SimClassTest.exe" "%HERE%..\Content\Data\Classes" || set FAILED=1

echo.
echo === maps ===
cl /nologo /EHsc /std:c++17 /W4 /D_CRT_SECURE_NO_WARNINGS /I"%PUB%" "%HERE%SimMapTest.cpp" "%PRIV%\SimTypes.cpp" "%PRIV%\SimUnit.cpp" "%PRIV%\SimAbility.cpp" "%PRIV%\SimMap.cpp" "%PRIV%\SimMovement.cpp" "%PRIV%\SimWorld.cpp" "%PRIV%\SimTargeting.cpp" "%PRIV%\SimResolve.cpp" "%PRIV%\SimAI.cpp" "%PRIV%\SimBattle.cpp" "%PRIV%\SimJson.cpp" "%PRIV%\SimClassFile.cpp" "%PRIV%\SimOrderText.cpp" "%PRIV%\SimItem.cpp" "%PRIV%\SimCamps.cpp" "%PRIV%\SimNeutral.cpp" "%PRIV%\SimStatuses.cpp" /Fo:"%OUT%\obj\\" /Fe:"%OUT%\SimMapTest.exe" >nul
if errorlevel 1 (echo BUILD FAILED & exit /b 1)
"%OUT%\SimMapTest.exe" "%HERE%..\Content\Data\Maps" || set FAILED=1

echo.
echo === whole battles, replayed from their recorded orders ===
cl /nologo /EHsc /std:c++17 /W4 /D_CRT_SECURE_NO_WARNINGS /I"%PUB%" "%HERE%SimTraceTest.cpp" "%PRIV%\SimTypes.cpp" "%PRIV%\SimUnit.cpp" "%PRIV%\SimAbility.cpp" "%PRIV%\SimMap.cpp" "%PRIV%\SimMovement.cpp" "%PRIV%\SimWorld.cpp" "%PRIV%\SimTargeting.cpp" "%PRIV%\SimResolve.cpp" "%PRIV%\SimAI.cpp" "%PRIV%\SimBattle.cpp" "%PRIV%\SimJson.cpp" "%PRIV%\SimClassFile.cpp" "%PRIV%\SimOrderText.cpp" "%PRIV%\SimItem.cpp" "%PRIV%\SimCamps.cpp" "%PRIV%\SimNeutral.cpp" "%PRIV%\SimStatuses.cpp" /Fo:"%OUT%\obj\\" /Fe:"%OUT%\SimTraceTest.exe" >nul
if errorlevel 1 (echo BUILD FAILED & exit /b 1)
"%OUT%\SimTraceTest.exe" "%HERE%..\Content\Data\Classes" "%HERE%Baselines\BattleTrace.txt" %REBASELINE% || set FAILED=1

echo.
echo === watchtowers ===
cl /nologo /EHsc /std:c++17 /W4 /D_CRT_SECURE_NO_WARNINGS /I"%PUB%" "%HERE%SimWatchtowerTest.cpp" "%PRIV%\SimTypes.cpp" "%PRIV%\SimUnit.cpp" "%PRIV%\SimAbility.cpp" "%PRIV%\SimMap.cpp" "%PRIV%\SimMovement.cpp" "%PRIV%\SimWorld.cpp" "%PRIV%\SimTargeting.cpp" "%PRIV%\SimResolve.cpp" "%PRIV%\SimAI.cpp" "%PRIV%\SimBattle.cpp" "%PRIV%\SimJson.cpp" "%PRIV%\SimClassFile.cpp" "%PRIV%\SimOrderText.cpp" "%PRIV%\SimItem.cpp" "%PRIV%\SimCamps.cpp" "%PRIV%\SimNeutral.cpp" "%PRIV%\SimStatuses.cpp" /Fo:"%OUT%\obj\\" /Fe:"%OUT%\SimWatchtowerTest.exe" >nul
if errorlevel 1 (echo BUILD FAILED & exit /b 1)
"%OUT%\SimWatchtowerTest.exe" "%HERE%..\Content\Data\Maps" || set FAILED=1

echo.
echo === items ===
cl /nologo /EHsc /std:c++17 /W4 /D_CRT_SECURE_NO_WARNINGS /I"%PUB%" "%HERE%SimItemTest.cpp" "%PRIV%\SimTypes.cpp" "%PRIV%\SimUnit.cpp" "%PRIV%\SimAbility.cpp" "%PRIV%\SimMap.cpp" "%PRIV%\SimMovement.cpp" "%PRIV%\SimWorld.cpp" "%PRIV%\SimTargeting.cpp" "%PRIV%\SimResolve.cpp" "%PRIV%\SimAI.cpp" "%PRIV%\SimBattle.cpp" "%PRIV%\SimJson.cpp" "%PRIV%\SimClassFile.cpp" "%PRIV%\SimOrderText.cpp" "%PRIV%\SimItem.cpp" "%PRIV%\SimCamps.cpp" "%PRIV%\SimNeutral.cpp" "%PRIV%\SimStatuses.cpp" /Fo:"%OUT%\obj\\" /Fe:"%OUT%\SimItemTest.exe" >nul
if errorlevel 1 (echo BUILD FAILED & exit /b 1)
"%OUT%\SimItemTest.exe" "%HERE%..\Content\Data\Items" "%HERE%..\Content\Data\Maps" || set FAILED=1

echo.
echo === neutral camps ===
cl /nologo /EHsc /std:c++17 /W4 /D_CRT_SECURE_NO_WARNINGS /I"%PUB%" "%HERE%SimCampTest.cpp" "%PRIV%\SimTypes.cpp" "%PRIV%\SimUnit.cpp" "%PRIV%\SimAbility.cpp" "%PRIV%\SimMap.cpp" "%PRIV%\SimMovement.cpp" "%PRIV%\SimWorld.cpp" "%PRIV%\SimTargeting.cpp" "%PRIV%\SimResolve.cpp" "%PRIV%\SimAI.cpp" "%PRIV%\SimBattle.cpp" "%PRIV%\SimJson.cpp" "%PRIV%\SimClassFile.cpp" "%PRIV%\SimOrderText.cpp" "%PRIV%\SimItem.cpp" "%PRIV%\SimCamps.cpp" "%PRIV%\SimNeutral.cpp" "%PRIV%\SimStatuses.cpp" /Fo:"%OUT%\obj\\" /Fe:"%OUT%\SimCampTest.exe" >nul
if errorlevel 1 (echo BUILD FAILED & exit /b 1)
"%OUT%\SimCampTest.exe" "%HERE%..\Content\Data\Items" "%HERE%..\Content\Data\Maps" || set FAILED=1

echo.
echo === the second set of statuses ===
cl /nologo /EHsc /std:c++17 /W4 /D_CRT_SECURE_NO_WARNINGS /I"%PUB%" "%HERE%SimStatusTest.cpp" "%PRIV%\SimTypes.cpp" "%PRIV%\SimUnit.cpp" "%PRIV%\SimAbility.cpp" "%PRIV%\SimMap.cpp" "%PRIV%\SimMovement.cpp" "%PRIV%\SimWorld.cpp" "%PRIV%\SimTargeting.cpp" "%PRIV%\SimResolve.cpp" "%PRIV%\SimAI.cpp" "%PRIV%\SimBattle.cpp" "%PRIV%\SimJson.cpp" "%PRIV%\SimClassFile.cpp" "%PRIV%\SimOrderText.cpp" "%PRIV%\SimItem.cpp" "%PRIV%\SimCamps.cpp" "%PRIV%\SimNeutral.cpp" "%PRIV%\SimStatuses.cpp" /Fo:"%OUT%\obj\\" /Fe:"%OUT%\SimStatusTest.exe" >nul
if errorlevel 1 (echo BUILD FAILED & exit /b 1)
"%OUT%\SimStatusTest.exe" "%HERE%..\Content\Data\Items" "%HERE%..\Content\Data\Maps" || set FAILED=1

echo.
echo === Cast Studio's published files (Source\TMCast) ===
cl /nologo /EHsc /std:c++17 /W4 /D_CRT_SECURE_NO_WARNINGS /I"%PUB%" /I"%HERE%..\Source\TMCast\Public" "%HERE%CastTest.cpp" "%HERE%..\Source\TMCast\Private\CastAnimation.cpp" "%HERE%..\Source\TMCast\Private\CastLooks.cpp" "%HERE%..\Source\TMCast\Private\CastLegacy.cpp" "%PRIV%\Sim*.cpp" /Fo:"%OUT%\obj\\" /Fe:"%OUT%\CastTest.exe" >nul
if errorlevel 1 (echo BUILD FAILED & exit /b 1)
"%OUT%\CastTest.exe" "%HERE%..\Content\Data\CastStudio" || set FAILED=1

echo.
if "%FAILED%"=="1" (echo SOME TESTS FAILED & exit /b 1)
echo ALL TESTS PASSED
exit /b 0