# Configure, build and test inside a VS 2022 developer environment.
#   .\build.ps1 [-Preset x64-Release] [-NoTest] [-Fetch]
param(
    [ValidateSet('x64-Debug', 'x64-Release', 'x86-Debug', 'x86-Release')]
    [string]$Preset = 'x64-Release',
    [switch]$NoTest,
    [switch]$Fetch
)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot

if ($Fetch -or -not (Test-Path "$root\third_party\librime\x64\lib\rime.lib")) { & "$root\third_party\fetch_librime.ps1" }
if ($Fetch -or -not (Test-Path "$root\data\rime-ice\t9.schema.yaml")) { & "$root\data\fetch_rime_ice.ps1" }

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -version '[17.0,18.0)' -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1
if (-not $vs) { throw 'Visual Studio 2022 with C++ tools not found' }
$arch = $Preset.Split('-')[0]
$hostArch = 'amd64'
$targetArch = if ($arch -eq 'x64') { 'amd64' } else { 'x86' }
Import-Module "$vs\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments "-arch=$targetArch -host_arch=$hostArch -no_logo" | Out-Null

Push-Location $root
try {
    cmake --preset $Preset
    if ($LASTEXITCODE) { throw 'configure failed' }
    cmake --build --preset $Preset
    if ($LASTEXITCODE) { throw 'build failed' }
    if (-not $NoTest) {
        ctest --preset $Preset -LE e2e
        if ($LASTEXITCODE) { throw 'tests failed' }
    }
} finally {
    Pop-Location
}
