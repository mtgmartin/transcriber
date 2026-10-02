# Drives the Transcriber window for a Phase 2 test: clear, arm, play for N seconds, stop, screenshot.
# Coordinates are for the plugin window at its usual place on the 2560x1440 monitor (see BUILD_PLAN §8).
# Usage: phase2-run.ps1 -Seconds 10 -Shot out.png [-NoClear] [-StopWith plugin]
param([double]$Seconds = 10, [string]$Shot, [switch]$NoClear, [switch]$NoPlay, [string]$Extra = "")
$inp = Join-Path $PSScriptRoot "input.ps1"
$ms = [int]($Seconds * 1000)
$steps = @("wait 100")
if (-not $NoClear) { $steps += "click 1478 206" }
$steps += "click 1136 60; click 1136 60; wait 300; click 1292 206; wait 400"
if (-not $NoPlay) { $steps += "click 1114 60; wait $ms; click 1136 60; wait 1500" }
if ($Extra) { $steps += $Extra }
& $inp ($steps -join "; ")
if ($Shot) { & (Join-Path $PSScriptRoot "shot.ps1") -Out $Shot -Scale 0.5 -X 1180 -Y 0 -W 1020 -H 1000 }
