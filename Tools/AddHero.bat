@echo off
REM Makes a Paragon hero from Fab into a body the classes can wear.
REM
REM   Tools\AddHero.bat <hero folder> <body name> [look ...] [--write]
REM
REM   Tools\AddHero.bat /Game/ParagonGreystone/Characters/Heroes/Greystone greystone knight squire --write
REM
REM Without --write it only says what it found and what it would write. Close
REM the editor first. Afterwards run Tools\AnimCatalog.bat and look at the new
REM body's strips in the class creator. See Docs\CharacterSetup.md.
setlocal
set LOG=%~dp0..\Saved\Logs\TacticalMasters.log
"E:\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%~dp0..\TacticalMasters.uproject" -run=pythonscript "-script=%~dp0add_hero.py %*" -unattended -nosplash > "%~dp0..\Saved\Logs\agent-addhero.log" 2>&1
findstr /C:"ADD HERO" "%LOG%"
findstr /C:"ADD HERO: DONE" "%LOG%" > nul
if errorlevel 1 exit /b 1
exit /b 0
