# Downloads the Windows 7 prerequisites bundled with the installer into
# third_party\prereq\ (not committed): the Platform Update KB2670838 (Direct2D /
# DirectWrite) for x64 and x86, from Microsoft's update servers. The SHA-1 in
# Microsoft's file name is the integrity check.
#   pwsh -File installer\fetch_prereqs.ps1
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$dir = Join-Path $root 'third_party\prereq'
New-Item -ItemType Directory -Force $dir | Out-Null

$packages = @(
    @{ Name = 'Windows6.1-KB2670838-x64.msu'; Sha1 = '9f667ff60e80b64cbed2774681302baeaf0fc6a6'
       Url = 'http://download.windowsupdate.com/msdownload/update/software/ftpk/2013/02/windows6.1-kb2670838-x64_9f667ff60e80b64cbed2774681302baeaf0fc6a6.msu' },
    @{ Name = 'Windows6.1-KB2670838-x86.msu'; Sha1 = '984b8d122a688d917f81c04155225b3ef31f012e'
       Url = 'http://download.windowsupdate.com/msdownload/update/software/ftpk/2013/02/windows6.1-kb2670838-x86_984b8d122a688d917f81c04155225b3ef31f012e.msu' }
)
foreach ($p in $packages) {
    $file = Join-Path $dir $p.Name
    if ((Test-Path $file) -and (Get-FileHash $file -Algorithm SHA1).Hash -eq $p.Sha1.ToUpper()) {
        Write-Host "ok        $($p.Name)"
        continue
    }
    Write-Host "download  $($p.Name)"
    Invoke-WebRequest -Uri $p.Url -OutFile "$file.part" -UseBasicParsing
    $hash = (Get-FileHash "$file.part" -Algorithm SHA1).Hash
    if ($hash -ne $p.Sha1.ToUpper()) {
        Remove-Item "$file.part"
        throw "$($p.Name): SHA-1 $hash, expected $($p.Sha1)"
    }
    Move-Item -Force "$file.part" $file
}
