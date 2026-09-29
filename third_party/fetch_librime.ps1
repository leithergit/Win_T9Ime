# Downloads the pinned official librime Windows release and verifies SHA-256.
# Output: third_party/librime/<arch>/{bin,include,lib}  and  third_party/librime/opencc/
# 7-Zip extraction keeps file mtimes (required: pre-deployed data must keep timestamps).
param(
    [ValidateSet('x64', 'x86', 'all')] [string]$Arch = 'all'
)
$ErrorActionPreference = 'Stop'

$Version = '1.17.0'
$Commit  = '33e7814'
$Assets = @{
    'x64'  = @{ File = "rime-$Commit-Windows-msvc-x64.7z";      Sha256 = '7478c7caa4ff6b37de86daba1f7ce4a994a4f5ba24872a820fb2b3a9b01fed15' }
    'x86'  = @{ File = "rime-$Commit-Windows-msvc-x86.7z";      Sha256 = 'af235c26c06152ce09ceb8fe9d9ab9fba7ab43ce30aa952b40174d806f5cc3d9' }
    'deps' = @{ File = "rime-deps-$Commit-Windows-msvc-x64.7z"; Sha256 = '9ef5608d8a54ff52bbad7a9b4128de42b232f8e3dd1f5fd3bff42a0b1bacd7e8' }
}

$Root = Join-Path $PSScriptRoot 'librime'
$Cache = Join-Path $Root '_download'
New-Item -ItemType Directory -Force $Cache | Out-Null

$SevenZip = @("$env:ProgramFiles\7-Zip\7z.exe", "${env:ProgramFiles(x86)}\7-Zip\7z.exe") |
    Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $SevenZip) { $SevenZip = (Get-Command 7z -ErrorAction SilentlyContinue).Source }
if (-not $SevenZip) { throw '7-Zip (7z.exe) is required.' }

function Get-Asset([string]$Key) {
    $a = $Assets[$Key]
    $path = Join-Path $Cache $a.File
    if (-not (Test-Path $path) -or (Get-FileHash $path -Algorithm SHA256).Hash -ne $a.Sha256.ToUpper()) {
        $url = "https://github.com/rime/librime/releases/download/$Version/$($a.File)"
        Write-Host "Downloading $url"
        Invoke-WebRequest -Uri $url -OutFile $path -UseBasicParsing
    }
    $hash = (Get-FileHash $path -Algorithm SHA256).Hash
    if ($hash -ne $a.Sha256.ToUpper()) { throw "SHA-256 mismatch for $($a.File): $hash" }
    return $path
}

function Expand-To([string]$Archive, [string]$Dest) {
    $tmp = Join-Path $Cache ('x_' + [IO.Path]::GetFileNameWithoutExtension($Archive))
    if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
    & $SevenZip x $Archive "-o$tmp" -y | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "7z failed on $Archive" }
    return $tmp
}

$archs = if ($Arch -eq 'all') { @('x64', 'x86') } else { @($Arch) }
foreach ($a in $archs) {
    $tmp = Expand-To (Get-Asset $a) $Root
    $dest = Join-Path $Root $a
    if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
    New-Item -ItemType Directory -Force "$dest\bin", "$dest\lib", "$dest\include" | Out-Null
    Copy-Item "$tmp\dist\lib\rime.dll", "$tmp\dist\bin\rime_deployer.exe", "$tmp\dist\bin\rime_dict_manager.exe" "$dest\bin"
    Copy-Item "$tmp\dist\lib\rime.lib" "$dest\lib"
    Copy-Item "$tmp\dist\include\*" "$dest\include" -Recurse
    if (Test-Path "$tmp\version-info.txt") { Copy-Item "$tmp\version-info.txt" $dest }
    Remove-Item -Recurse -Force $tmp
    Write-Host "librime $Version ($a) -> $dest"
}

# OpenCC data (architecture independent) for simplifier@traditionalize.
$tmp = Expand-To (Get-Asset 'deps') $Root
$opencc = Join-Path $Root 'opencc'
if (Test-Path $opencc) { Remove-Item -Recurse -Force $opencc }
New-Item -ItemType Directory -Force $opencc | Out-Null
foreach ($f in 's2t.json', 'STCharacters.ocd2', 'STPhrases.ocd2') {
    $src = Get-ChildItem -Recurse -File $tmp -Filter $f | Select-Object -First 1
    if (-not $src) { throw "opencc file $f not found in deps package" }
    Copy-Item $src.FullName $opencc
}
Remove-Item -Recurse -Force $tmp
Write-Host "opencc -> $opencc"
