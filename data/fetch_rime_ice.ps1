# Fetches rime-ice at a pinned commit into data/rime-ice (commit hash is the integrity check).
$ErrorActionPreference = 'Stop'

$Repo   = 'https://github.com/iDvel/rime-ice.git'
$Commit = '3aea6d3694fb3d94ec663641f021f788822897ad'
$Dest   = Join-Path $PSScriptRoot 'rime-ice'

if (Test-Path (Join-Path $Dest '.git')) {
    $head = (git -C $Dest rev-parse HEAD).Trim()
    if ($head -eq $Commit) { Write-Host "rime-ice already at $Commit"; exit 0 }
    Remove-Item -Recurse -Force $Dest
}
New-Item -ItemType Directory -Force $Dest | Out-Null
git -C $Dest init -q
git -C $Dest remote add origin $Repo
git -C $Dest fetch -q --depth 1 origin $Commit
if ($LASTEXITCODE -ne 0) { throw 'git fetch failed' }
git -C $Dest -c advice.detachedHead=false checkout -q FETCH_HEAD
$head = (git -C $Dest rev-parse HEAD).Trim()
if ($head -ne $Commit) { throw "rime-ice HEAD $head != $Commit" }
Write-Host "rime-ice $Commit -> $Dest"
