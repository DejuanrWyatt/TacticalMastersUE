@echo off
REM Copies every sound in the project out as a .wav for the class creator.
REM
REM The creator runs in a browser, which cannot open a SoundWave or SoundCue.
REM So the game is started with -tmsoundcatalog: it writes each sound's audio
REM as a plain wave file into Saved\SoundCatalog, lists them in catalog.json,
REM and exits by itself. Run it again after adding sounds from Fab; the
REM creator's Sound effects panel has a button that does the same.
REM
REM It must be the editor binary: a packaged game keeps no copy of the audio
REM it can write out. Nothing is played, so -nosound is fine.
setlocal
set LOG=%~dp0..\Saved\Logs\agent-sound-catalog.log
echo Copying the project's sounds ...
"E:\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%~dp0..\TacticalMasters.uproject" /Game/Maps/Showcase -game -unattended -RenderOffscreen -nosound -tmsoundcatalog -stdout -FullStdOutLogOutput > "%LOG%" 2>&1
findstr /C:"SOUND STUDIO DONE" "%LOG%"
if errorlevel 1 (echo THE SOUNDS WERE NOT COPIED -- see %LOG% & exit /b 1)
exit /b 0
