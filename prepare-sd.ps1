# Copies the app and the minimum set of files it needs from YOUR Geometry Dash install
# onto an SD card (or any folder). Nothing is uploaded; the files stay on your own devices.
#
#   .\prepare-sd.ps1 -Sd E:\                      # SD card root
#   .\prepare-sd.ps1 -Sd E:\ -Levels C:\my\levels # also copy .gdlvl and .aiogdl files
#   .\prepare-sd.ps1 -Sd E:\ -Music C:\my\music   # also copy your own songs (.mp3 .ogg .wav)
#
# The online levels saved in your own Geometry Dash are written to the card as level files (only
# the levels themselves, nothing about your account); -NoSavedLevels leaves them out.
param(
    [Parameter(Mandatory = $true)][string]$Sd,
    [string]$Steam = 'C:\Program Files (x86)\Steam\steamapps\common\Geometry Dash',
    [string]$Levels = '',
    [string]$Music = '',
    [switch]$NoSavedLevels
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$res = Join-Path $Steam 'Resources'
if (-not (Test-Path $res)) { throw "Resources folder not found: $res" }

$nro = Join-Path $root 'gd-switch2.nro'
if (-not (Test-Path $nro)) { throw "Build the app first (gd-switch2.nro is missing)." }

$dest = Join-Path $Sd 'switch\gd'
$dres = Join-Path $dest 'Resources'
New-Item -ItemType Directory -Force "$dres\levels", "$dest\levels", "$dest\music" | Out-Null

# Sprite sheets (HD), fonts, backgrounds, ground tiles, level files.
$patterns = @(
    'GJ_GameSheet-hd.*', 'GJ_GameSheet02-hd.*', 'GJ_GameSheet03-hd.*', 'GJ_GameSheet04-hd.*',
    'GJ_GameSheetGlow-hd.*', 'GJ_GameSheetIcons-hd.*', 'GJ_LaunchSheet-hd.*', 'FireSheet_01-hd.*', 'PixelSheet_01-hd.*', 'GJ_ParticleSheet-hd.*',
    'bigFont-hd.*', 'goldFont-hd.*', 'square02_001-hd.png', 'GJ_progressBar_001-hd.png', 'smallDot-hd.png', 'slidergroove2-hd.png', 'sliderBar2-hd.png', 'GJ_gradientBG-hd.png',
    'GJ_square05-hd.png', 'GJ_button_01-hd.png', 'GJ_button_04-hd.png', 'square02b_001-hd.png',
    'GJ_square01-hd.png', 'slidergroove-hd.png', 'sliderthumb-hd.png',
    'playSound_01.ogg', 'quitSound_01.ogg', 'explode_11.ogg',
    # the songs of the official levels and the menu music
    'menuLoop.mp3', 'StereoMadness.mp3', 'BackOnTrack.mp3', 'Polargeist.mp3', 'DryOut.mp3', 'BaseAfterBase.mp3',
    'CantLetGo.mp3', 'Jumper.mp3', 'TimeMachine.mp3', 'Cycles.mp3', 'xStep.mp3', 'Clutterfunk.mp3',
    'TheoryOfEverything.mp3', 'Electroman.mp3', 'Clubstep.mp3', 'Electrodynamix.mp3', 'HexagonForce.mp3',
    'BlastProcessing.mp3', 'TheoryOfEverything2.mp3', 'GeometricalDominator.mp3', 'Deadlocked.mp3',
    'Fingerdash.mp3', 'Dash.mp3',
    'groundSquare_*_001-hd.png', 'game_bg_*_001-hd.png',
    'GJBeast0?_AnimDesc.plist', 'Robot_AnimDesc.plist', 'Spider_AnimDesc.plist'
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

# Player icons: one file per icon, like the original loads them.
New-Item -ItemType Directory -Force "$dres\icons" | Out-Null
foreach ($f in Get-ChildItem "$res\icons" -Filter '*-hd.*' -File) {
    Copy-Item $f.FullName "$dres\icons" -Force
    $count++
}

# Your own songs, chosen in the game on the page of a custom level.
if ($Music -and (Test-Path $Music)) {
    foreach ($f in Get-ChildItem $Music -File | Where-Object { $_.Extension -in '.mp3', '.ogg', '.wav' }) {
        Copy-Item -LiteralPath $f.FullName "$dest\music" -Force
    }
}

# Custom levels (.gdlvl, .aiogdl) + the list file the console build reads.
if ($Levels -and (Test-Path $Levels)) {
    foreach ($f in Get-ChildItem $Levels -File | Where-Object { $_.Extension -in '.gdlvl', '.aiogdl' }) {
        Copy-Item -LiteralPath $f.FullName "$dest\levels" -Force
    }
}
if (-not $NoSavedLevels) {
    $exe = @("$root\build\gd.exe", "$root\gd.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
    if ($exe) { & $exe --export-saved "$dest\levels" }
    else { Write-Warning "gd.exe not found: the levels saved in your game are not copied (run build-pc.ps1)." }
}
$list = @(Get-ChildItem "$dest\levels" -File | Where-Object { $_.Extension -in '.gdlvl', '.aiogdl' } |
    ForEach-Object { "sdmc:/switch/gd/levels/$($_.Name)" })
[System.IO.File]::WriteAllLines("$dest\levels\levels.txt", [string[]]$list)

Copy-Item $nro (Join-Path $Sd 'switch\gd-switch2.nro') -Force
Write-Host "Copied $count resource files and the app to $Sd"
Write-Host "Start kaiHEN, open hbmenu (hold R while launching a game) and run 'Geometry Dash (fan port)'."
