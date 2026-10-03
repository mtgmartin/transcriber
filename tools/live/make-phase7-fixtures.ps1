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

# t43-edit-pages (Phase 7d): 24 bars of 4/4, a C major scale in quarter notes up and down, left hand C3 and G2 half notes.
$r = @()
$scale = @(60, 62, 64, 65, 67, 69, 71, 72, 71, 69, 67, 65, 64, 62, 60, 62)
for ($beat = 0; $beat -lt 96; $beat++) { $r += , @($scale[$beat % 16], [double]$beat, 1.0, 90) }
for ($half = 0; $half -lt 48; $half++) { $r += , @($(if ($half % 2 -eq 0) { 48 } else { 43 }), ($half * 2.0), 2.0, 80) }
Write-MidiFile (Join-Path $out "t43-edit-pages.mid") $r 96.0
if (Test-Path $live) { Copy-Item (Join-Path $out "t43-edit-pages.mid") -Destination $live -Force }

# t44-edit-guitar (Phase 7e): 3 bars of 4/4 for a guitar.
#  bar 1: the open strings E2 A2 D3 G3 (quarters)
#  bar 2: A2 + E3 as a chord (half), B3 and C4 (quarters)
#  bar 3: eighths E3 G3 A3 B3 A3 G3 E3 D3
$g = @(
  @(40, 0.0, 1.0, 95), @(45, 1.0, 1.0, 95), @(50, 2.0, 1.0, 95), @(55, 3.0, 1.0, 95),
  @(45, 4.0, 2.0, 100), @(52, 4.0, 2.0, 100), @(59, 6.0, 1.0, 95), @(60, 7.0, 1.0, 95)
)
$riff = @(52, 55, 57, 59, 57, 55, 52, 50)
for ($i = 0; $i -lt 8; $i++) { $g += , @($riff[$i], (8.0 + $i * 0.5), 0.5, 95) }
Write-MidiFile (Join-Path $out "t44-edit-guitar.mid") $g 12.0
if (Test-Path $live) { Copy-Item (Join-Path $out "t44-edit-guitar.mid") -Destination $live -Force }

# t45-edit-bass (Phase 7e): 3 bars of 4/4 for a bass.
#  bar 1: the open strings E1 A1 D2 G2 (quarters); bar 2: E1 half, G1, A1 quarters; bar 3: eighths E1 E1 G1 A1 B1 A1 G1 E1
$b = @(
  @(28, 0.0, 1.0, 95), @(33, 1.0, 1.0, 95), @(38, 2.0, 1.0, 95), @(43, 3.0, 1.0, 95),
  @(28, 4.0, 2.0, 100), @(31, 6.0, 1.0, 95), @(33, 7.0, 1.0, 95)
)
$line = @(28, 28, 31, 33, 35, 33, 31, 28)
for ($i = 0; $i -lt 8; $i++) { $b += , @($line[$i], (8.0 + $i * 0.5), 0.5, 95) }
Write-MidiFile (Join-Path $out "t45-edit-bass.mid") $b 12.0
if (Test-Path $live) { Copy-Item (Join-Path $out "t45-edit-bass.mid") -Destination $live -Force }
