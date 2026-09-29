@echo off
REM Imports the Godot game's sound effects into /Game/Audio/SFX (Tools\import_sounds.py),
REM then writes Content\Data\Sounds\sounds.json (Tools\assign_sounds.py): each ability's
REM sounds, the game's moments, and each hero's voice from its pack. Close the editor first.
setlocal
"E:\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%~dp0..\TacticalMasters.uproject" -run=pythonscript "-script=%~dp0import_sounds.py" -unattended -nosplash > "%~dp0..\Saved\Logs\agent-import-sounds.log" 2>&1
findstr /C:"IMPORT SOUNDS" "%~dp0..\Saved\Logs\agent-import-sounds.log"
python "%~dp0assign_sounds.py"
