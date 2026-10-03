# Writes the Phase 8 test clips into tests/fixtures and copies them to the Live test project.
# Usage: powershell -ExecutionPolicy Bypass -File tools\live\make-phase8-fixtures.ps1
. "$PSScriptRoot\midi.ps1"

$root = Resolve-Path "$PSScriptRoot\..\.."
$out = Join-Path $root "tests\fixtures"
$live = Join-Path $root "test Project\Transcriber tests"

function Save([string]$name, $notes, [double]$length) {
  Write-MidiFile (Join-Path $out $name) $notes $length
  if (Test-Path $live) { Copy-Item (Join-Path $out $name) -Destination $live -Force }
}

# t81-fine-run (Phase 8c): 2 bars of 4/4 for a piano, a C major run in very short notes.
#  bar 1: eight 64th notes on beat 1 (C4 D4 E4 F4 G4 A4 B4 C5), a quarter E4 on beat 2, eight 128th notes on beat 3, a quarter G4 on beat 4
#  bar 2: six 64th-note triplets on beat 1 (C4 D4 E4 F4 G4 A4), then a C4 whole-note tie over the rest of the bar
$scale = @(60, 62, 64, 65, 67, 69, 71, 72)
$f = @()
for ($i = 0; $i -lt 8; $i++) { $f += , @($scale[$i], ($i * 0.0625), 0.0625, 95) }
$f += , @(64, 1.0, 1.0, 100)
for ($i = 0; $i -lt 8; $i++) { $f += , @($scale[$i], (2.0 + $i * 0.03125), 0.03125, 95) }
$f += , @(67, 3.0, 1.0, 100)
for ($i = 0; $i -lt 6; $i++) { $f += , @($scale[$i], (4.0 + $i / 24.0), (1.0 / 24.0), 95) }
$f += , @(60, 5.0, 3.0, 100)
Save "t81-fine-run.mid" $f 8.0

# t82-drum-roll (Phase 8c): 2 bars of 4/4 for drums. Bar 1: a kick on beats 1 and 3, a snare roll in 32nd notes over beats 2-3.
#  bar 2: a snare roll in 64th notes over beat 1, a crash on beat 3.
$d = @(@(36, 0.0, 0.25, 110), @(36, 2.0, 0.25, 110))
for ($i = 0; $i -lt 8; $i++) { $d += , @(38, (1.0 + $i * 0.125), 0.1, 90) }
for ($i = 0; $i -lt 16; $i++) { $d += , @(38, (4.0 + $i * 0.0625), 0.05, 85) }
$d += , @(49, 6.0, 0.25, 110)
Save "t82-drum-roll.mid" $d 8.0

Get-ChildItem $out -Filter "t8*.mid" | Select-Object Name, Length

# t83-scale (Phase 8e): 2 bars of 4/4, a C major scale in quarter notes from C4 to C5.
$sc = @(60, 62, 64, 65, 67, 69, 71, 72)
$t = @()
for ($i = 0; $i -lt 8; $i++) { $t += , @($sc[$i], [double]$i, 1.0, 95) }
Save "t83-scale.mid" $t 8.0
Get-ChildItem $out -Filter "t83*.mid" | Select-Object Name, Length
