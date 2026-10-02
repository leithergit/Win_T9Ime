# Builds the T9Ime installer (Inno Setup 6) from the x64-Release and x86-Release builds.
#   pwsh -File tools\make_installer.ps1            use the existing builds
#   pwsh -File tools\make_installer.ps1 -Build     build both presets first (with tests)
# Output: dist\installer\T9Ime-<version>-Setup.exe, plus a size report.
param([switch]$Build)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent

$iscc = @("${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:ProgramFiles\Inno Setup 6\ISCC.exe") |
    Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) { throw 'Inno Setup 6 not found (https://jrsoftware.org/isdl.php)' }

if ($Build) {
    foreach ($preset in 'x64-Release', 'x86-Release') {
        & pwsh -NoProfile -File (Join-Path $root 'build.ps1') -Preset $preset
        if ($LASTEXITCODE) { throw "build $preset failed" }
    }
}

# Version: project(VERSION) + git commit count, as in CMakeLists.txt.
$project = Select-String -Path (Join-Path $root 'CMakeLists.txt') -Pattern '^project\(T9Ime VERSION ([0-9.]+)' |
    Select-Object -First 1
$base = $project.Matches[0].Groups[1].Value
$count = (git -C $root rev-list --count HEAD).Trim()
$commit = (git -C $root rev-parse --short HEAD).Trim()
$version = "$base.$count"

$x64 = Join-Path $root 'out\build\x64-Release'
$x86 = Join-Path $root 'out\build\x86-Release'
foreach ($dir in $x64, $x86) {
    foreach ($f in 'bin\T9Host.exe', 'bin\T9Tip.dll', 'bin\T9Ctl.dll', 'bin\rime.dll', 'src\ctl\T9Ctl.lib') {
        if (-not (Test-Path (Join-Path $dir $f))) { throw "missing $dir\$f - build first (or pass -Build)" }
    }
    # The binaries must be built from this commit: their version resource says so.
    $built = (Get-Item (Join-Path $dir 'bin\T9Tip.dll')).VersionInfo.FileVersion
    if ($built -ne $version) {
        throw "$dir was built as $built, the source is at $version - rebuild (or pass -Build)"
    }
}
if (git -C $root status --porcelain) { Write-Warning 'working tree has uncommitted changes' }

# Windows 7 Platform Update KB2670838 bundled with the setup (installer\fetch_prereqs.ps1).
$prereq = Join-Path $root 'third_party\prereq'
& (Join-Path $root 'installer\fetch_prereqs.ps1')

$out = Join-Path $root 'dist\installer'
New-Item -ItemType Directory -Force $out | Out-Null
& $iscc /Qp "/DAppVersion=$version" "/DCommit=$commit" "/DX64Bin=$x64\bin" "/DX86Bin=$x86\bin" `
    "/DX64Lib=$x64\src\ctl" "/DX86Lib=$x86\src\ctl" "/DSrcRoot=$root" "/DPrereqDir=$prereq" "/DOutputDir=$out" `
    (Join-Path $root 'installer\T9Ime.iss')
if ($LASTEXITCODE) { throw 'ISCC failed' }

$setup = Get-Item (Join-Path $out "T9Ime-$version-Setup.exe")
$hash = (Get-FileHash $setup.FullName -Algorithm SHA256).Hash

# Size report: what the installer carries (uncompressed) and what it weighs.
function SizeOf($path) { (Get-ChildItem $path -Recurse -File | Measure-Object Length -Sum).Sum }
$report = @(
    "T9Ime $version (commit $commit) installer",
    "  file     : $($setup.Name)",
    ("  size     : {0:N1} MB" -f ($setup.Length / 1MB)),
    "  sha256   : $hash",
    "  contents (uncompressed):",
    ("    data/            {0,8:N1} MB" -f ((SizeOf "$x64\bin\data") / 1MB)),
    ("    rime.dll x64/x86 {0,8:N1} MB" -f (((Get-Item "$x64\bin\rime.dll").Length + (Get-Item "$x86\bin\rime.dll").Length) / 1MB)),
    ("    T9Host x64/x86   {0,8:N1} MB" -f (((Get-Item "$x64\bin\T9Host.exe").Length + (Get-Item "$x86\bin\T9Host.exe").Length) / 1MB)),
    ("    T9Tip x64/x86    {0,8:N2} MB" -f (((Get-Item "$x64\bin\T9Tip.dll").Length + (Get-Item "$x86\bin\T9Tip.dll").Length) / 1MB)),
    ("    KB2670838 x64/x86 {0,7:N1} MB (installed on Windows 7 only when missing)" -f ((SizeOf $prereq) / 1MB))
)
$report | Set-Content -Encoding utf8 (Join-Path $out "T9Ime-$version-Setup.txt")
$report | ForEach-Object { Write-Host $_ }
