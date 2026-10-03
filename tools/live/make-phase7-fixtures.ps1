# Writes the Phase 7 test clip (a piano part to edit) into tests/fixtures and copies it to the Live test project.
# Usage: powershell -ExecutionPolicy Bypass -File tools\live\make-phase7-fixtures.ps1
. "$PSScriptRoot\midi.ps1"

$root = Resolve-Path "$PSScriptRoot\..\.."
$out = Join-Path $root "tests\fixtures"
$live = Join-Path $root "test Project\Transcriber tests"

# t41-edit-practice: 3 bars of 4/4 in C major.
#  bar 1: C major chord (quarter), D4 quarter, E4 half          (right hand)
#  bar 2: C4 C4 (two quarters to tie), F4 G4 eighths, A4 half-ish (a quarter and a rest)
#  bar 3: C5 whole
#  left hand: C3 whole note in every bar
$p = @(
  @(60, 0.0, 1.0, 100), @(64, 0.0, 1.0, 100), @(67, 0.0, 1.0, 100), @(62, 1.0, 1.0, 95), @(64, 2.0, 2.0, 95),
  @(60, 4.0, 1.0, 100), @(60, 5.0, 1.0, 100), @(65, 6.0, 0.5, 95), @(67, 6.5, 0.5, 95), @(69, 7.0, 1.0, 95),
  @(72, 8.0, 4.0, 100),
  @(48, 0.0, 4.0, 85), @(48, 4.0, 4.0, 85), @(48, 8.0, 4.0, 85)
)
Write-MidiFile (Join-Path $out "t41-edit-practice.mid") $p 12.0

if (Test-Path $live) {
  Get-ChildItem $out -Filter "t41*.mid" | Copy-Item -Destination $live -Force
  "Copied to $live"
}
Get-ChildItem $out -Filter "t41*.mid" | Select-Object Name, Length

# t42-edit-layout (Phase 7b): 3 bars of 4/4, right hand only plus C3 whole notes in the left hand.
#  bar 1: eight eighth notes C4 D4 E4 F4 G4 A4 B4 C5 (beams, voices)
#  bar 2: F#4 G4 A#4 B4 quarters (spelling, key)
#  bar 3: C#5 half, C#5 half (spelling of tied notes)
$q = @()
$pitches = @(60, 62, 64, 65, 67, 69, 71, 72)
for ($i = 0; $i -lt 8; $i++) { $q += , @($pitches[$i], ($i * 0.5), 0.5, 95) }
$q += , @(66, 4.0, 1.0, 100); $q += , @(67, 5.0, 1.0, 100); $q += , @(70, 6.0, 1.0, 100); $q += , @(71, 7.0, 1.0, 100)
$q += , @(73, 8.0, 2.0, 100); $q += , @(73, 10.0, 2.0, 100)
$q += , @(48, 0.0, 4.0, 85); $q += , @(48, 4.0, 4.0, 85); $q += , @(48, 8.0, 4.0, 85)
Write-MidiFile (Join-Path $out "t42-edit-layout.mid") $q 12.0
if (Test-Path $live) { Copy-Item (Join-Path $out "t42-edit-layout.mid") -Destination $live -Force }
