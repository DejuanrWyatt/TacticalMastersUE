@echo off
REM Films every animation clip the character map uses, on the bodies that wear
REM them, for the class creator.
REM
REM The creator runs in a browser, which cannot play an Unreal animation. So the
REM game is started with -tmanimcatalog: it poses each body through each clip in
REM front of a camera, saves the frames as a strip of pictures, writes
REM Saved\AnimCatalog\catalog.json, and exits by itself. Run it again after
REM changing Content\Data\CharacterMap\characters.json or adding a hero; the
REM creator's Effects tab has a button that does the same.
REM
REM It draws off-screen, so no window appears, but it needs the graphics card:
REM -nullrhi would film nothing.
setlocal
set LOG=%~dp0..\Saved\Logs\agent-anim-catalog.log
echo Filming the characters' animations ...
"E:\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%~dp0..\TacticalMasters.uproject" /Game/Maps/Showcase -game -unattended -RenderOffscreen -NoTextureStreaming -nosound -tmanimcatalog -stdout -FullStdOutLogOutput > "%LOG%" 2>&1
findstr /C:"ANIM STUDIO DONE" "%LOG%"
if errorlevel 1 (echo THE ANIMATIONS WERE NOT FILMED -- see %LOG% & exit /b 1)
exit /b 0
