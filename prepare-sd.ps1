# Copies the app and the minimum set of files it needs from YOUR Geometry Dash install
# onto an SD card (or any folder). Nothing is uploaded; the files stay on your own devices.
#
#   .\prepare-sd.ps1 -Sd E:\                      # SD card root
#   .\prepare-sd.ps1 -Sd E:\ -Levels C:\my\levels # also copy .gdlvl files
param(
    [Parameter(Mandatory = $true)][string]$Sd,
    [string]$Steam = 'C:\Program Files (x86)\Steam\steamapps\common\Geometry Dash',
    [string]$Levels = ''
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$res = Join-Path $Steam 'Resources'
if (-not (Test-Path $res)) { throw "Resources folder not found: $res" }

$nro = Join-Path $root 'gd-switch2.nro'
if (-not (Test-Path $nro)) { throw "Build the app first (gd-switch2.nro is missing)." }

$dest = Join-Path $Sd 'switch\gd'
$dres = Join-Path $dest 'Resources'
New-Item -ItemType Directory -Force "$dres\levels", "$dest\levels" | Out-Null

# Sprite sheets (HD), fonts, backgrounds, ground tiles, level files.
$patterns = @(
    'GJ_GameSheet-hd.*', 'GJ_GameSheet02-hd.*', 'GJ_GameSheet03-hd.*', 'GJ_GameSheet04-hd.*',
    'GJ_GameSheetGlow-hd.*', 'GJ_GameSheetIcons-hd.*', 'GJ_LaunchSheet-hd.*', 'FireSheet_01-hd.*', 'PixelSheet_01-hd.*', 'GJ_ParticleSheet-hd.*',
    'bigFont-hd.*', 'GJ_gradientBG-hd.png', 'GJ_square01-hd.png',
    'groundSquare_*_001-hd.png', 'game_bg_*_001-hd.png'
)
$count = 0
foreach ($p in $patterns) {
    foreach ($f in Get-ChildItem $res -Filter $p -File) {
        Copy-Item $f.FullName $dres -Force
        $count++
    }
}
foreach ($f in Get-ChildItem "$res\levels" -Filter '*.txt') {
    Copy-Item $f.FullName "$dres\levels" -Force
    $count++
}

# Custom levels (.gdlvl) + the list file the console build reads.
$list = @()
if ($Levels -and (Test-Path $Levels)) {
    foreach ($f in Get-ChildItem $Levels -Filter '*.gdlvl') {
        Copy-Item $f.FullName "$dest\levels" -Force
        $list += "sdmc:/switch/gd/levels/$($f.Name)"
    }
}
[System.IO.File]::WriteAllLines("$dest\levels\levels.txt", [string[]]$list)

Copy-Item $nro (Join-Path $Sd 'switch\gd-switch2.nro') -Force
Write-Host "Copied $count resource files and the app to $Sd"
Write-Host "Start kaiHEN, open hbmenu (hold R while launching a game) and run 'Geometry Dash (fan port)'."
