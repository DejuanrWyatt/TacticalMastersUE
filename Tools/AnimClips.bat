@echo off
REM Films just the clips Cast Studio's designer picked and has not seen yet, for
REM the class creator ("Film the picked clips"). They are listed one a line,
REM "<animation set> <clip object path>", in Saved\AnimCatalog\wanted.txt, which
REM the creator writes. The game is started off-screen with -tmanimwanted: it
REM films them on their bodies, writes Saved\AnimCatalog\catalog-wanted-*.json
REM beside the catalogue (which stays as it is), and exits by itself.
REM
REM The full filming (AnimCatalog.bat) starts the folder afresh, and films every
REM clip the published picks name, so nothing published is lost by it.
setlocal
echo Filming the picked clips ...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0AnimClips.ps1"
exit /b %ERRORLEVEL%
