# Session View recording: clear, arm, launch the clip in the track's slot, wait, press the plugin's Stop, stop Live, screenshot.
# Usage: phase2-session.ps1 -SlotY 121 -Seconds 8.5 -Shot out.png [-SlotX 983] [-Launch2Y 193 -Launch2At 15.1] [-TransportStop]
# -Launch2Y/-Launch2At: launch a second clip that many seconds after the first (clip sequences).
# -TransportStop: end by stopping Live's transport instead of the plugin's Stop button.
param([int]$SlotY, [double]$Seconds, [string]$Shot, [int]$SlotX = 983, [int]$Launch2Y = 0, [double]$Launch2At = 0, [switch]$TransportStop)
$inp = Join-Path $PSScriptRoot "input.ps1"
& $inp "click 1136 60; click 1136 60; wait 300; click 1292 206; wait 400; click $SlotX $SlotY"
$t0 = Get-Date
if ($Launch2Y -gt 0) {
  Start-Sleep -Milliseconds ([int]($Launch2At * 1000))
  & $inp "click $SlotX $Launch2Y"
}
$left = $Seconds - ((Get-Date) - $t0).TotalSeconds
if ($left -gt 0) { Start-Sleep -Milliseconds ([int]($left * 1000)) }
if ($TransportStop) { & $inp "click 1136 60; wait 1200" } else { & $inp "click 1292 206; wait 800; click 1136 60; click 1136 60; wait 500" }
if ($Shot) { & (Join-Path $PSScriptRoot "shot.ps1") -Out $Shot -Scale 0.5 -X 1180 -Y 0 -W 1020 -H 1000 }
