# Clicks "Compare with MIDI file…" in the open plugin window and enters the path in the file dialog.
# Usage: compare-file.ps1 -Path <mid file> [-X 1786 -Y 166]   (X/Y: the button's screen position)
param([string]$Path, [int]$X = 1330, [int]$Y = 286)
$inputScript = Join-Path $PSScriptRoot "input.ps1"
$saved = Get-Clipboard -Raw -ErrorAction SilentlyContinue
Set-Clipboard -Value (Resolve-Path $Path).Path
& $inputScript "click $X $Y; wait 1500; key ctrl+v; wait 300; key enter; wait 1500"
if ($saved) { Set-Clipboard -Value $saved }
