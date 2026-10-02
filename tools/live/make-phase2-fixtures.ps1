# Writes the Phase 2 test clips into tests/fixtures (and copies them to the Live test project).
# Every bar of a clip is different, so a repeat is only found when the clip really repeats.
# Usage: powershell -ExecutionPolicy Bypass -File tools\live\make-phase2-fixtures.ps1
. "$PSScriptRoot\midi.ps1"

$root = Resolve-Path "$PSScriptRoot\..\.."
$out = Join-Path $root "tests\fixtures"
$live = Join-Path $root "test Project\Transcriber tests"

# Three notes per bar (as in tests/core/test_main.cpp makeClip); variant shifts two of the pitches.
function Make-BarNotes([int]$bars, [int]$variant = 0) {
  $notes = @()
  for ($j = 0; $j -lt $bars; $j++) {
    $b = $j * 4.0
    $notes += , @((48 + $j % 24 + $variant), ($b + 0.0), 1.0, 90)
    $notes += , @((60 + ($j * 3) % 11 + $variant), ($b + 1.5), 0.5, 100)
    $notes += , @((72 + ($j * 5) % 13), ($b + 2.75), 0.25, 110)
  }
  return , $notes
}

foreach ($bars in 1, 2, 4, 8) {
  Write-MidiFile (Join-Path $out "t21-loop$bars.mid") (Make-BarNotes $bars) ($bars * 4.0)
}
# Same length as loop4 but every note one semitone higher: stands in for "the clip was edited".
Write-MidiFile (Join-Path $out "t22-loop4-edited.mid") (Make-BarNotes 4 1) 16.0
Write-MidiFile (Join-Path $out "t22-verse.mid") (Make-BarNotes 4 0) 16.0
Write-MidiFile (Join-Path $out "t22-chorus.mid") (Make-BarNotes 4 5) 16.0

# Three minutes at 120 BPM = 90 bars.
$rng = New-Object System.Random 20261001

# Piano: a bass note, a chord and a melody that moves; every 8th bar a triplet figure.
$piano = @()
for ($j = 0; $j -lt 90; $j++) {
  $b = $j * 4.0
  $root = 36 + (($j * 5) % 12)
  $piano += , @($root, $b, 2.0, 80)
  foreach ($interval in 0, 4, 7) { $piano += , @((60 + (($j * 3) % 7) + $interval), ($b + 2.0), 1.0, 70) }
  if ($j % 8 -eq 7) {
    for ($k = 0; $k -lt 6; $k++) { $piano += , @((72 + $k * 2), ($b + 0.0 + $k / 3.0), 0.25, 90) }
  } else {
    $step = 0.5 * $rng.Next(1, 4)
    $pos = 0.0
    while ($pos -lt 3.5) {
      $piano += , @((74 + $rng.Next(0, 9)), ($b + $pos), 0.25, (60 + $rng.Next(0, 50)))
      $pos += $step
    }
  }
}
Write-MidiFile (Join-Path $out "t23-piano-3min.mid") $piano 360.0

# Drums (General MIDI): kick 36, snare 38, closed hat 42, open hat 46, crash 49, toms 45/47/48.
$drums = @()
for ($j = 0; $j -lt 90; $j++) {
  $b = $j * 4.0
  for ($beat = 0; $beat -lt 8; $beat++) { $drums += , @(42, ($b + $beat * 0.5), 0.1, (70 + 30 * ($beat % 2))) }
  $drums += , @(36, $b, 0.1, 110); $drums += , @(36, ($b + 2.0), 0.1, 105)
  if ($j % 2 -eq 1) { $drums += , @(36, ($b + 2.5), 0.1, 95) }
  $drums += , @(38, ($b + 1.0), 0.1, 110); $drums += , @(38, ($b + 3.0), 0.1, 110)
  if ($j % 4 -eq 3) {
    foreach ($t in @(@(48, 3.0), @(47, 3.25), @(45, 3.5), @(45, 3.75))) { $drums += , @($t[0], ($b + $t[1]), 0.1, 100) }
  }
  if ($j % 8 -eq 0) { $drums += , @(49, $b, 0.5, 115) }
  if ($j % 8 -eq 5) { $drums += , @(46, ($b + 3.5), 0.4, 90) }
  if ($j % 6 -eq 2) { $drums += , @(38, ($b + 3.75), 0.1, 40) }   # a ghost note
}
Write-MidiFile (Join-Path $out "t23-drums-3min.mid") $drums 360.0

if (Test-Path $live) {
  Get-ChildItem $out -Filter "t2*.mid" | Copy-Item -Destination $live -Force
  "Copied to $live"
}
Get-ChildItem $out -Filter "t2*.mid" | Select-Object Name, Length

# 2 bars of 4/4 whose last note runs to the end of the clip (for the held-note-at-stop test).
$held = @()
foreach ($n in @(@(60, 0.0, 1.0, 100), @(64, 2.0, 1.0, 100), @(62, 4.0, 1.0, 100), @(67, 6.0, 2.0, 100))) { $held += , $n }
Write-MidiFile (Join-Path $out "t24-held-note.mid") $held 8.0
if (Test-Path $live) { Copy-Item (Join-Path $out "t24-held-note.mid") $live -Force }
