@echo off
REM Makes an environment pack from Fab into a theme built from its meshes.
REM
REM   Tools\AddKit.bat <pack folder> <theme id> [--base ruined_keep] [--name Agora] [--write]
REM
REM   Tools\AddKit.bat /Game/ParagonAgora agora --base ruined_keep --name "Agora" --write
REM
REM Without --write it only says what it found. Close the editor first. Then
REM play a battle in the theme (-tmtheme=agora, or Theme on the setup screen).
REM See Docs\Maps.md.
setlocal
set LOG=%~dp0..\Saved\Logs\TacticalMasters.log
"E:\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%~dp0..\TacticalMasters.uproject" -run=pythonscript "-script=%~dp0add_env_kit.py %*" -unattended -nosplash > "%~dp0..\Saved\Logs\agent-addkit.log" 2>&1
findstr /C:"ADD KIT" "%LOG%"
findstr /C:"ADD KIT: DONE" "%LOG%" > nul
if errorlevel 1 exit /b 1
exit /b 0
