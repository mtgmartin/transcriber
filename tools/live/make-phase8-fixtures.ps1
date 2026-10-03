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

# t84-low-guitar (Phase 8f): 3 bars of 4/4 for a guitar with notes below the low E: bar 1 D2 A2 D3 G3 (Drop D), bar 2 B1 E2 A2 (needs seven strings), bar 3 a chord D2+A2+D3 and E3.
$lg = @(
  @(38, 0.0, 1.0, 95), @(45, 1.0, 1.0, 95), @(50, 2.0, 1.0, 95), @(55, 3.0, 1.0, 95),
  @(35, 4.0, 1.0, 95), @(40, 5.0, 1.0, 95), @(45, 6.0, 2.0, 95),
  @(38, 8.0, 2.0, 100), @(45, 8.0, 2.0, 100), @(50, 8.0, 2.0, 100), @(52, 10.0, 2.0, 95)
)
Save "t84-low-guitar.mid" $lg 12.0

# t85-low-bass (Phase 8f): 2 bars of 4/4 for a bass: bar 1 D1 A1 D2 G2 (Drop D), bar 2 B0 E1 A1 D2 (needs five strings).
$lb = @(
  @(26, 0.0, 1.0, 95), @(33, 1.0, 1.0, 95), @(38, 2.0, 1.0, 95), @(43, 3.0, 1.0, 95),
  @(23, 4.0, 1.0, 95), @(28, 5.0, 1.0, 95), @(33, 6.0, 1.0, 95), @(38, 7.0, 1.0, 95)
)
Save "t85-low-bass.mid" $lb 8.0
Get-ChildItem $out -Include "t84*.mid","t85*.mid" -Recurse | Select-Object Name, Length
