@echo off
REM Remakes /Game/UI/M_TeamOutline from Tools\make_outline_material.py (2026-10-06: smoother, steadier outline).
REM Close the Unreal editor first. Log: E:\Builds\outline.log
setlocal
set ROOT=E:\UnrealProjects\TacticalMastersUE
tasklist /fi "imagename eq UnrealEditor.exe" | find /i "UnrealEditor.exe" >nul
if not errorlevel 1 (echo The Unreal editor is open; close it and run this again. & pause & exit /b 1)
"E:\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%ROOT%\TacticalMasters.uproject" -run=pythonscript -script="%ROOT%\Tools\make_outline_material.py" -unattended -nosplash > E:\Builds\outline.log 2>&1
findstr /c:"OUTLINE:" E:\Builds\outline.log
pause
