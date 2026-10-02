# Compares the score notes with a MIDI file and screenshots the plugin window.
# Usage: check.ps1 -Path <mid> -Shot out.png [-Y 802]   (Y: the compare button; +14 if the reading text has 3 lines)
param([string]$Path, [string]$Shot, [int]$Y = 286)
$dir = $PSScriptRoot
& (Join-Path $dir "compare-file.ps1") -Path $Path -Y $Y
& (Join-Path $dir "shot.ps1") -Out $Shot -Scale 0.5 -X 1180 -Y 0 -W 1020 -H 1000
