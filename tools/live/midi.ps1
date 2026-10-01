# Writes a format-0 Standard MIDI File. Notes: array of @(pitch, startBeats, durBeats, velocity).
function Write-MidiFile([string]$Path, [object[]]$Notes, [double]$LengthBeats, [int]$Tpq = 960) {
  $events = New-Object System.Collections.Generic.List[object]
  foreach ($n in $Notes) {
    $on = [int][Math]::Round($n[1] * $Tpq); $off = [int][Math]::Round(($n[1] + $n[2]) * $Tpq)
    $events.Add(@($on, 1, [byte]0x90, [byte]$n[0], [byte]$n[3]))
    $events.Add(@($off, 0, [byte]0x80, [byte]$n[0], [byte]0))   # note-offs sort before note-ons at the same tick
  }
  $sorted = $events | Sort-Object @{e={$_[0]}}, @{e={$_[1]}}
  $track = New-Object System.Collections.Generic.List[byte]
  function VLQ([int]$v) { $b = @([byte]($v -band 0x7F)); $v = $v -shr 7; while ($v -gt 0) { $b = @([byte](($v -band 0x7F) -bor 0x80)) + $b; $v = $v -shr 7 }; return $b }
  # 4/4 time signature and 120 BPM tempo meta events at tick 0
  $track.AddRange([byte[]](0x00, 0xFF, 0x58, 0x04, 0x04, 0x02, 0x18, 0x08))
  $track.AddRange([byte[]](0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20))
  $t = 0
  foreach ($e in $sorted) { $track.AddRange([byte[]](VLQ ($e[0] - $t))); $track.AddRange([byte[]]($e[2], $e[3], $e[4])); $t = $e[0] }
  $end = [int][Math]::Round($LengthBeats * $Tpq)
  $track.AddRange([byte[]](VLQ ([Math]::Max(0, $end - $t)))); $track.AddRange([byte[]](0xFF, 0x2F, 0x00))
  $out = New-Object System.Collections.Generic.List[byte]
  $out.AddRange([Text.Encoding]::ASCII.GetBytes("MThd")); $out.AddRange([byte[]](0,0,0,6, 0,0, 0,1, [byte]($Tpq -shr 8), [byte]($Tpq -band 0xFF)))
  $out.AddRange([Text.Encoding]::ASCII.GetBytes("MTrk")); $len = $track.Count
  $out.AddRange([byte[]]([byte](($len -shr 24) -band 0xFF), [byte](($len -shr 16) -band 0xFF), [byte](($len -shr 8) -band 0xFF), [byte]($len -band 0xFF)))
  $out.AddRange($track)
  [IO.File]::WriteAllBytes($Path, $out.ToArray())
}
