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
REM
REM A few heroes per run (Tools\AnimCatalog.ps1): the editor build cannot hold
REM every hero's clips at once.
setlocal
echo Filming the characters' animations ...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0AnimCatalog.ps1"
exit /b %ERRORLEVEL%
