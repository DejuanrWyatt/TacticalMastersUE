@echo off
REM Films every particle effect in the project for the class creator.
REM
REM The creator runs in a browser, which cannot play a Niagara or Cascade system.
REM So the game is started with -tmvfxcatalog: it plays each effect alone in front
REM of a camera, saves two seconds of it as a strip of pictures, writes
REM Saved\VfxCatalog\catalog.json, and exits by itself. Run it again after adding
REM effects from Fab; the creator's Effects tab has a button that does the same.
REM
REM It draws off-screen, so no window appears, but it does need the graphics
REM card: -nullrhi would film nothing. Texture streaming is off because the
REM studio's camera is not a view the streamer serves: it would film blurred
REM low-resolution textures.
setlocal
set LOG=%~dp0..\Saved\Logs\agent-vfx-catalog.log
echo Filming the project's effects ...
"E:\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%~dp0..\TacticalMasters.uproject" /Game/Maps/Showcase -game -unattended -RenderOffscreen -NoTextureStreaming -nosound -tmvfxcatalog -stdout -FullStdOutLogOutput > "%LOG%" 2>&1
findstr /C:"VFX STUDIO DONE" "%LOG%"
if errorlevel 1 (echo THE EFFECTS WERE NOT FILMED -- see %LOG% & exit /b 1)
exit /b 0
