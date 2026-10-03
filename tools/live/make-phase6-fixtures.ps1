# Writes the Phase 6 test clips (drums, guitar riff, bass line) into tests/fixtures and copies them to the Live test project.
# Usage: powershell -ExecutionPolicy Bypass -File tools\live\make-phase6-fixtures.ps1
. "$PSScriptRoot\midi.ps1"

$root = Resolve-Path "$PSScriptRoot\..\.."
$out = Join-Path $root "tests\fixtures"
$live = Join-Path $root "test Project\Transcriber tests"

# A rock groove with a fill (General MIDI drums: kick 36, snare 38, closed hat 42, open hat 46, crash 49, toms 50/48/45).
# 4 bars in 4/4: hi-hat eighths, kick on 1 and 3 (plus "and of 3" in bar 2), snare on 2 and 4, a ghost snare, a crash on 1, a fill in bar 4.
$d = @()
for ($bar = 0; $bar -lt 4; $bar++) {
  $b = $bar * 4.0
  for ($i = 0; $i -lt 8; $i++) {
    if ($bar -eq 3 -and $i -ge 6) { continue }
    $d += , @(42, ($b + $i * 0.5), 0.1, (80 + 20 * ($i % 2)))
  }
  $d += , @(36, $b, 0.1, 110); $d += , @(36, ($b + 2.0), 0.1, 105)
  if ($bar -eq 1) { $d += , @(36, ($b + 2.5), 0.1, 100) }
  $d += , @(38, ($b + 1.0), 0.1, 112)
  if ($bar -lt 3) { $d += , @(38, ($b + 3.0), 0.1, 112) }
  if ($bar -eq 2) { $d += , @(38, ($b + 3.75), 0.1, 35) }       # ghost note
  if ($bar -eq 1) { $d += , @(46, ($b + 3.5), 0.4, 90) }         # open hi-hat
}
$d += , @(49, 0.0, 1.0, 120)                                      # crash on the first beat
foreach ($t in @(@(38, 3.0), @(50, 3.25), @(48, 3.5), @(45, 3.75))) { $d += , @($t[0], (12.0 + $t[1]), 0.2, 100) }   # fill
Write-MidiFile (Join-Path $out "t31-drum-groove.mid") $d 16.0

# A guitar riff in E minor, 2 bars: low E and A, a power chord, an open chord, a melody line.
$g = @(
  @(40, 0.0, 0.5, 100), @(40, 0.5, 0.5, 90), @(43, 1.0, 0.5, 95), @(45, 1.5, 0.5, 95),          # E2 E2 G2 A2
  @(47, 2.0, 1.0, 100),                                                                           # B2
  @(40, 3.0, 1.0, 105), @(47, 3.0, 1.0, 105), @(52, 3.0, 1.0, 105),                              # E power chord (E2 B2 E3)
  @(40, 4.0, 2.0, 100), @(47, 4.0, 2.0, 100), @(52, 4.0, 2.0, 100), @(56, 4.0, 2.0, 100), @(59, 4.0, 2.0, 100), @(64, 4.0, 2.0, 100),   # open E major chord
  @(64, 6.0, 0.5, 100), @(67, 6.5, 0.5, 100), @(71, 7.0, 0.5, 100), @(76, 7.5, 0.5, 100)         # E4 G4 B4 E5
)
Write-MidiFile (Join-Path $out "t31-guitar-riff.mid") $g 8.0

# A bass line in A minor, 2 bars: root-fifth pattern with passing notes, one octave jump.
$bs = @(
  @(33, 0.0, 0.75, 100), @(33, 0.75, 0.25, 90), @(40, 1.0, 0.5, 100), @(43, 1.5, 0.5, 100),     # A1 A1 E2 G2
  @(45, 2.0, 1.0, 100), @(40, 3.0, 0.5, 95), @(38, 3.5, 0.5, 95),                                # A2 E2 D2
  @(29, 4.0, 1.0, 100), @(29, 5.0, 0.5, 100), @(36, 5.5, 0.5, 100), @(41, 6.0, 1.0, 100), @(31, 7.0, 1.0, 100)   # F1 F1 C2 F2 G1
)
Write-MidiFile (Join-Path $out "t31-bassline.mid") $bs 8.0

if (Test-Path $live) {
  Get-ChildItem $out -Filter "t31*.mid" | Copy-Item -Destination $live -Force
  "Copied to $live"
}
Get-ChildItem $out -Filter "t31*.mid" | Select-Object Name, Length
