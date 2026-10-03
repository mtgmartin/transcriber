# Records one take from a Session clip, for a plugin window of the tall layout (the Score panel on top).
# Usage: record-slot.ps1 -SlotY 122 -Seconds 17 [-SlotX 983] [-RecordX 1292 -RecordY 742]
# The window layout changes with the instrument (the drum map row adds height), so check the Record button first.
param([int]$SlotY, [double]$Seconds, [int]$SlotX = 983, [int]$RecordX = 1292, [int]$RecordY = 742)
$inp = Join-Path $PSScriptRoot "input.ps1"
& $inp "click 1136 60; click 1136 60; wait 300; click $RecordX $RecordY; wait 400; click $SlotX $SlotY"
Start-Sleep -Milliseconds ([int]($Seconds * 1000))
& $inp "click $RecordX $RecordY; wait 800; click 1136 60; click 1136 60; wait 1500"
