# Builds a portable test package for real-device testing (Windows 7 touch devices).
#   .\tools\make_test_package.ps1 -Milestone M1
# Output: dist\<Milestone>\T9Ime-<Milestone>-test.zip containing x86\ and x64\ builds,
# the shared data directory, regression scripts with expected output, and run.bat.
param([string]$Milestone = 'M1')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$dist = Join-Path $root "dist\$Milestone"
$stage = Join-Path $dist 'T9Ime-test'
if (Test-Path $dist) { Remove-Item -Recurse -Force $dist }
New-Item -ItemType Directory -Force $stage | Out-Null

foreach ($arch in 'x86', 'x64') {
    $build = Join-Path $root "out\build\$arch-Release"
    if (-not (Test-Path "$build\tools\t9repl\t9repl.exe")) { throw "build $arch-Release first" }
    New-Item -ItemType Directory -Force "$stage\$arch" | Out-Null
    Copy-Item "$build\tools\t9repl\t9repl.exe", "$build\tools\t9repl\rime.dll" "$stage\$arch"
}
# Data is architecture independent. robocopy /COPY:DAT keeps timestamps.
robocopy (Join-Path $root 'out\build\x64-Release\data') "$stage\data" /E /COPY:DAT /DCOPY:T /XF .t9ime-data-stamp /NFL /NDL /NJH /NJS | Out-Null
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

$zip = Join-Path $dist "T9Ime-$Milestone-test.zip"
$sevenZip = "$env:ProgramFiles\7-Zip\7z.exe"
& $sevenZip a -tzip -mx=9 $zip "$stage\*" | Out-Null
if ($LASTEXITCODE) { throw '7z failed' }
Write-Host "package: $zip ($([math]::Round((Get-Item $zip).Length / 1MB, 1)) MB)"
