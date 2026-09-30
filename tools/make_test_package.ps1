# Builds a portable test package for real-device testing (Windows 7 touch devices).
#   .\tools\make_test_package.ps1 -Milestone M1
# Output: dist\<Milestone>\T9Ime-<Milestone>-test.zip containing x86\ and x64\ builds,
# the shared data directory, regression scripts with expected output, run.bat
# (engine tests) and panel.bat (touch panel with a test target window).
param([string]$Milestone = 'M1')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$dist = Join-Path $root "dist\$Milestone"
$stage = Join-Path $dist 'T9Ime-test'
if (Test-Path $dist) { Remove-Item -Recurse -Force $dist }
New-Item -ItemType Directory -Force $stage | Out-Null

foreach ($arch in 'x86', 'x64') {
    $bin = Join-Path $root "out\build\$arch-Release\bin"
    if (-not (Test-Path "$bin\T9Host.exe")) { throw "build $arch-Release first" }
    New-Item -ItemType Directory -Force "$stage\$arch" | Out-Null
    Copy-Item "$bin\t9repl.exe", "$bin\rime.dll", "$bin\T9Host.exe", "$bin\test_target.exe", "$bin\T9Tip.dll", "$bin\t9diag.exe" "$stage\$arch"
    # Control API (M5): DLL, command line, samples.
    Copy-Item "$bin\T9Ctl.dll", "$bin\t9ctl.exe", "$bin\TestHost.exe" "$stage\$arch"
    if (Test-Path "$bin\TestHost.CS.exe") { Copy-Item "$bin\TestHost.CS.exe", "$bin\TestHost.CS.exe.config" "$stage\$arch" }
}
New-Item -ItemType Directory -Force "$stage\sdk" | Out-Null
Copy-Item "$root\src\ctl\t9ctl.h", "$root\samples\TestHost\cs\T9Ctl.cs" "$stage\sdk"
# Data is architecture independent. robocopy /COPY:DAT keeps timestamps.
robocopy (Join-Path $root 'out\build\x64-Release\bin\data') "$stage\data" /E /COPY:DAT /DCOPY:T /XF .t9ime-data-stamp /NFL /NDL /NJH /NJS | Out-Null
if ($LASTEXITCODE -ge 8) { throw 'robocopy failed' }

New-Item -ItemType Directory -Force "$stage\tests" | Out-Null
foreach ($t in Get-ChildItem "$root\tests\regress\*.t9") {
    Copy-Item $t.FullName "$stage\tests"
    # Expected output with CRLF line endings, as t9repl writes on Windows.
    $expected = [IO.File]::ReadAllText(($t.FullName -replace '\.t9$', '.expected')) -replace "`r?`n", "`r`n"
    [IO.File]::WriteAllText("$stage\tests\$($t.BaseName).expected", $expected, (New-Object Text.UTF8Encoding $false))
}

@'
@echo off
rem T9Ime engine test. Run on the test device; send back the whole "results" folder.
setlocal
cd /d "%~dp0"
set ARCH=x86
if /i "%PROCESSOR_ARCHITECTURE%"=="AMD64" set ARCH=x64
if /i "%PROCESSOR_ARCHITEW6432%"=="AMD64" set ARCH=x64
if not "%1"=="" set ARCH=%1
if exist results rmdir /s /q results
mkdir results
ver > results\system.txt
echo ARCH=%ARCH% >> results\system.txt
set FAIL=0
for %%T in (tests\*.t9) do (
  "%ARCH%\t9repl.exe" --data data --user "%TEMP%\t9ime-test-user\%%~nT" --fresh --script "%%T" > "results\%%~nT.txt" 2> "results\%%~nT.err"
  fc /b "results\%%~nT.txt" "tests\%%~nT.expected" > nul
  if errorlevel 1 (echo FAIL %%~nT & set FAIL=1) else (echo PASS %%~nT)
  type "results\%%~nT.err"
)
if "%FAIL%"=="0" (echo ALL PASSED) else (echo SOME TESTS FAILED - send the results folder back)
endlocal
pause
'@ | Set-Content -Encoding ascii "$stage\run.bat"

# Touch panel launcher: T9Host with a throw-away user directory; the input mode
# can be forced (touch = WM_TOUCH, mouse = promoted mouse messages, pointer = Win8+).
@'
@echo off
rem Usage: panel.bat [touch|mouse|pointer]   (default: automatic)
setlocal
cd /d "%~dp0"
set ARCH=x86
if /i "%PROCESSOR_ARCHITECTURE%"=="AMD64" set ARCH=x64
if /i "%PROCESSOR_ARCHITEW6432%"=="AMD64" set ARCH=x64
set MODE=
if not "%1"=="" set MODE=--input %1
taskkill /im T9Host.exe /f > nul 2>&1
start "" "%ARCH%\T9Host.exe" --data "%~dp0data" --user "%TEMP%\t9ime-panel-user" --settings "%TEMP%\t9ime-panel.ini" --show %MODE%
start "" "%ARCH%\test_target.exe"
endlocal
'@ | Set-Content -Encoding ascii "$stage\panel.bat"

# Test window with T9Ime already active in it (Windows 7 keeps one input
# method per application, so switching elsewhere does not affect it).
@'
@echo off
setlocal
cd /d "%~dp0"
set ARCH=x86
if /i "%PROCESSOR_ARCHITECTURE%"=="AMD64" set ARCH=x64
if /i "%PROCESSOR_ARCHITEW6432%"=="AMD64" set ARCH=x64
start "" "%ARCH%\test_target.exe" --activate-tip
endlocal
'@ | Set-Content -Encoding ascii "$stage\target.bat"

# TIP registration (needs "Run as administrator"). 64-bit Windows registers both
# DLLs so 32-bit applications work too.
@'
@echo off
rem Right-click -> Run as administrator. Registers the T9Ime input method.
setlocal
cd /d "%~dp0"
if exist "%SystemRoot%\SysWOW64\regsvr32.exe" (
  "%SystemRoot%\System32\regsvr32.exe" /s "%~dp0x64\T9Tip.dll" || goto fail
  "%SystemRoot%\SysWOW64\regsvr32.exe" /s "%~dp0x86\T9Tip.dll" || goto fail
) else (
  "%SystemRoot%\System32\regsvr32.exe" /s "%~dp0x86\T9Tip.dll" || goto fail
)
rem Start the host now (the installer will start it at logon). Through
rem explorer.exe so it runs with the normal (not elevated) user token.
set ARCH=x86
if /i "%PROCESSOR_ARCHITECTURE%"=="AMD64" set ARCH=x64
if /i "%PROCESSOR_ARCHITEW6432%"=="AMD64" set ARCH=x64
tasklist /fi "imagename eq T9Host.exe" | find /i "T9Host.exe" > nul || explorer.exe "%~dp0%ARCH%\T9Host.exe"
echo Registered. Choose "T9Ime" in the language bar (Chinese - Simplified).
pause
exit /b 0
:fail
echo Registration FAILED - did you run it as administrator?
pause
exit /b 1
'@ | Set-Content -Encoding ascii "$stage\register.bat"
@'
@echo off
rem Right-click -> Run as administrator. Removes the T9Ime input method.
setlocal
taskkill /im T9Host.exe /f > nul 2>&1
if exist "%SystemRoot%\SysWOW64\regsvr32.exe" (
  "%SystemRoot%\System32\regsvr32.exe" /s /u "%~dp0x64\T9Tip.dll"
  "%SystemRoot%\SysWOW64\regsvr32.exe" /s /u "%~dp0x86\T9Tip.dll"
) else (
  "%SystemRoot%\System32\regsvr32.exe" /s /u "%~dp0x86\T9Tip.dll"
)
echo Unregistered. Log off or restart before deleting this folder.
pause
'@ | Set-Content -Encoding ascii "$stage\unregister.bat"

$zip = Join-Path $dist "T9Ime-$Milestone-test.zip"
$sevenZip = "$env:ProgramFiles\7-Zip\7z.exe"
& $sevenZip a -tzip -mx=9 $zip "$stage\*" | Out-Null
if ($LASTEXITCODE) { throw '7z failed' }
Write-Host "package: $zip ($([math]::Round((Get-Item $zip).Length / 1MB, 1)) MB)"
