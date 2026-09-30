# Registers (or unregisters) the development build of T9Tip.dll for x64 and,
# when built, x86. Needs administrator rights (run from an elevated shell, or
# let it re-launch itself elevated).
#   .\tools\dev_register.ps1 [-Unregister] [-Config Release]
param([switch]$Unregister, [string]$Config = 'Release')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) {
    $args = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $PSCommandPath, '-Config', $Config)
    if ($Unregister) { $args += '-Unregister' }
    $p = Start-Process pwsh -ArgumentList $args -Verb RunAs -Wait -PassThru
    exit $p.ExitCode
}
$flag = if ($Unregister) { '/u' } else { '' }
$targets = @(
    @{ Dll = "$root\out\build\x64-$Config\bin\T9Tip.dll"; Regsvr = "$env:SystemRoot\System32\regsvr32.exe" },
    @{ Dll = "$root\out\build\x86-$Config\bin\T9Tip.dll"; Regsvr = "$env:SystemRoot\SysWOW64\regsvr32.exe" }
)
$log = Join-Path $root 'out\dev_register.log'
"$(Get-Date) unregister=$Unregister" | Set-Content $log
foreach ($t in $targets) {
    if (-not (Test-Path $t.Dll)) { "skip $($t.Dll)" | Add-Content $log; continue }
    $a = @('/s') + $(if ($flag) { @($flag) } else { @() }) + @("`"$($t.Dll)`"")
    $p = Start-Process $t.Regsvr -ArgumentList $a -Wait -PassThru
    "$($t.Dll): exit $($p.ExitCode)" | Add-Content $log
    if ($p.ExitCode -ne 0) { exit $p.ExitCode }
}
exit 0
