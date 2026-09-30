@echo off
rem T9Ime M5 test (control API) on a Windows 7 touch device (or VM with the touch screen
rem passed through). Double-click; then use your finger. Results are written
rem to the "results" folder next to this file.
setlocal enabledelayedexpansion
set CLSID={BB2F3BA4-3B7A-414A-A98F-08C100E5447E}
set SRC=%~dp0T9Ime-test
set RES=%~dp0results
if not exist "%RES%" mkdir "%RES%"

if exist "%SRC%\VERSION.txt" type "%SRC%\VERSION.txt"
echo.
echo [1/5] Stopping T9Host and unregistering the previous build...
taskkill /im T9Host.exe /f > nul 2>&1
for /f "tokens=2,*" %%a in ('reg query "HKCR\CLSID\%CLSID%\InprocServer32" /ve 2^>nul ^| find "REG_SZ"') do (
  "%SystemRoot%\System32\regsvr32.exe" /s /u "%%b"
)
if exist "%SystemRoot%\SysWOW64\regsvr32.exe" (
  for /f "tokens=2,*" %%a in ('reg query "HKCR\Wow6432Node\CLSID\%CLSID%\InprocServer32" /ve 2^>nul ^| find "REG_SZ"') do (
    "%SystemRoot%\SysWOW64\regsvr32.exe" /s /u "%%b"
  )
)

echo [2/5] Copying the package to the local disk...
rem A new folder every time: DLLs of the previous build stay loaded until restart.
set DST=C:\T9Ime-touch-%RANDOM%%RANDOM%
xcopy /e /i /q /y "%SRC%" "!DST!" > nul
if errorlevel 1 (
  echo Copy FAILED.
  pause
  exit /b 1
)

echo [3/5] Registering !DST! ...
set ARCH=x86
if /i "%PROCESSOR_ARCHITECTURE%"=="AMD64" set ARCH=x64
if /i "%PROCESSOR_ARCHITEW6432%"=="AMD64" set ARCH=x64
if exist "%SystemRoot%\SysWOW64\regsvr32.exe" (
  "%SystemRoot%\System32\regsvr32.exe" /s "!DST!\x64\T9Tip.dll" || goto regfail
  "%SystemRoot%\SysWOW64\regsvr32.exe" /s "!DST!\x86\T9Tip.dll" || goto regfail
) else (
  "%SystemRoot%\System32\regsvr32.exe" /s "!DST!\x86\T9Tip.dll" || goto regfail
)

echo [4/5] Resetting panel settings (touch pop-up on, "always" off) and starting T9Host...
rem --take-over-touch-keyboard: hides the Windows Input Panel icon next to text
rem boxes and its screen-edge tab (undo: tray icon menu, uncheck the item).
if exist "%APPDATA%\T9Ime\panel.ini" del "%APPDATA%\T9Ime\panel.ini"
start "" "!DST!\!ARCH!\T9Host.exe" --take-over-touch-keyboard
"!DST!\!ARCH!\t9diag.exe" > "%RES%\diag_before.txt" 2>&1

echo [5/5] Recording for 10 minutes (results\m5_watch.txt) and opening the sample programs...
start "T9Ime watch - do not close" /min cmd /c ""!DST!\!ARCH!\t9diag.exe" --watch 600 > "%RES%\m5_watch.txt" 2>&1"
start "" "!DST!\!ARCH!\TestHost.exe"
start "" "!DST!\!ARCH!\TestHost.CS.exe"
echo.
echo Follow Docs\M5-checklist.md (copied next to this file as M5-checklist.md):
echo   A. the two TestHost windows: every button, with your FINGER
echo   B. command line: open a command prompt in
echo      !DST!\!ARCH!
echo      and run t9ctl installed / show number / status / pos 100 100 / watch 60 ...
echo Write down the result of each step.
pause
exit /b 0

:regfail
echo Registration FAILED.
pause
exit /b 1
