@echo off
REM The balance pass (Tools\ClassLab\balance.py): screen every class in the class lab, shrink the noise out,
REM confirm the outliers on fresh battles, and write Saved\Balance\balance.md (and .csv, .json). It changes no
REM class file. Builds the lab first if it is missing. Takes a while: about 100 classes x 80 battles, then the
REM outliers again. Extra options pass through, e.g.  Balance.bat --search --ratings
REM See Docs\design\feat-class-balance.md, "The lab".
setlocal
set ROOT=%~dp0..\..
if not exist "%ROOT%\Binaries\ClassLab\TMClassLab.exe" call "%~dp0Build.bat" || exit /b 1
where python >nul 2>&1
if errorlevel 1 (echo Python 3 is needed for the balance pass. & exit /b 1)
python "%~dp0balance.py" %*
