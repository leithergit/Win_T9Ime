@echo off
rem T9Ime touch test on a Windows 7 touch device (or VM with the touch screen
rem passed through). Double-click; then use your finger. Results are written
rem to the "results" folder next to this file.
setlocal enabledelayedexpansion
set CLSID={BB2F3BA4-3B7A-414A-A98F-08C100E5447E}
set SRC=%~dp0T9Ime-test
set RES=%~dp0results
if not exist "%RES%" mkdir "%RES%"

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

echo [5/5] Recording for 5 minutes (results\watch.txt) and opening the test window...
start "T9Ime watch - do not close" /min cmd /c ""!DST!\!ARCH!\t9diag.exe" --watch 300 > "%RES%\watch.txt" 2>&1"
start "" "!DST!\!ARCH!\test_target.exe" --activate-tip
echo.
echo Now use your FINGER:
echo   1. tap the big text box      - the nine-key panel should pop up
echo   2. tap the middle number box - the panel should switch to digits
echo   3. tap the bottom password box - the panel shows letter keys (abc, def ...)
echo      tap "abc" twice quickly = b, "def" once = d, the case key (right column,
echo      middle) then "abc" = A,
echo      hold a key = its digit. The password box should show 3 dots, and the
echo      Windows Input Panel icon must NOT appear next to the box.
echo   4. tap the big text box again, type 94664486 on the panel and tap the first candidate
echo   5. tap the desktop           - the panel should hide
echo   6. tap the big text box, hide the panel with its hide key, switch the input
echo      method to English with the language bar (or Ctrl+Shift), wait 2 seconds,
echo      then switch back to T9Ime - the panel should pop up by itself
echo Then write down what happened for each step. Recording stops after 5 minutes.
pause
exit /b 0

:regfail
echo Registration FAILED.
pause
exit /b 1
