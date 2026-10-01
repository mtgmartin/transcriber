param([string]$Path)

$lines = Get-Content $Path
$recs = foreach ($l in $lines) { if ($l.Trim()) { $l | ConvertFrom-Json } }

"=== $([IO.Path]::GetFileName($Path))  ($($recs.Count) records)"

$events = $recs | Where-Object { $_.t -eq 'event' }
$blocks = @($recs | Where-Object { $_.t -eq 'block' })
$midi   = @($recs | Where-Object { $_.t -eq 'midi' })

"--- events by type"
$events | Group-Object type | Sort-Object Count -Descending | ForEach-Object { "  {0,-16} {1}" -f $_.Name, $_.Count }

"--- key events (non-ui first)"
foreach ($e in $events) {
    switch ($e.type) {
        'start'        { "  start: " + ($e.data | ConvertTo-Json -Compress) }
        'prepare'      { "  prepare: " + ($e.data | ConvertTo-Json -Compress) }
        'editorOpened' { "  editorOpened: " + ($e.data | ConvertTo-Json -Compress) }
        'getState'     { "  getState: " + ($e.data | ConvertTo-Json -Compress) }
        'setState'     { "  setState: " + ($e.data | ConvertTo-Json -Compress) }
        'compare'      { "  compare: " + ($e.data | ConvertTo-Json -Compress -Depth 4) }
        'pdfSaved'     { "  pdfSaved: " + ($e.data | ConvertTo-Json -Compress) }
        'stateTestSize'{ "  stateTestSize: " + $e.data }
    }
}

"--- ui events (excluding key)"
$events | Where-Object { $_.type -eq 'ui' -and $_.data.type -ne 'key' } | ForEach-Object { "  ui: " + ($_.data | ConvertTo-Json -Compress) } | Select-Object -Unique -First 40

"--- keys seen in page"
$keys = $events | Where-Object { $_.type -eq 'ui' -and $_.data.type -eq 'key' -and $_.data.event -eq 'keydown' } | ForEach-Object { $_.data.code + $(if ($_.data.mods) { '+' + $_.data.mods }) }
"  " + (($keys | Select-Object -Unique) -join ', ')

"--- resize events"
$events | Where-Object { $_.type -eq 'resize' } | ForEach-Object { "  " + ($_.data | ConvertTo-Json -Compress) } | Select-Object -Unique -First 10

"--- blocks: $($blocks.Count)  midi: $($midi.Count)"
if ($blocks.Count) {
    $fields = 'hasPos','playing','looping','recording','offline','ppq','bpm','ts','barPpq','bars','loopStart','loopEnd','samples','hostNs'
    foreach ($f in $fields) {
        $present = @($blocks | Where-Object { $null -ne $_.$f })
        $trueCount = @($blocks | Where-Object { $_.$f -eq $true })
        "  {0,-10} present in {1,6} blocks; true in {2,6}" -f $f, $present.Count, $trueCount.Count
    }
    "  block sizes: " + (($blocks | Group-Object n | Sort-Object Count -Descending | Select-Object -First 4 | ForEach-Object { "$($_.Name)x$($_.Count)" }) -join ', ')
    "  bpm values: " + (($blocks | Where-Object { $null -ne $_.bpm } | Group-Object bpm | Select-Object -First 8 | ForEach-Object { $_.Name }) -join ', ')
    "  ts values: " + (($blocks | Where-Object { $null -ne $_.ts } | Group-Object ts | ForEach-Object { "$($_.Name)x$($_.Count)" }) -join ', ')
    $lp = $blocks | Where-Object { $null -ne $_.loopStart } | Group-Object { "$($_.loopStart)-$($_.loopEnd)" }
    "  loop points: " + (($lp | ForEach-Object { "$($_.Name)x$($_.Count)" }) -join ', ')

    # Position jumps while playing: backwards jumps indicate loop wraps.
    $playing = @($blocks | Where-Object { $_.playing -and $null -ne $_.ppq })
    $jumps = @()
    for ($i = 1; $i -lt $playing.Count; $i++) {
        $d = $playing[$i].ppq - $playing[$i-1].ppq
        if ($d -lt 0 -and ($playing[$i].block - $playing[$i-1].block) -eq 1) { $jumps += "{0:N3} -> {1:N3} (looping={2})" -f $playing[$i-1].ppq, $playing[$i].ppq, $playing[$i].looping }
    }
    "  backward jumps while playing: $($jumps.Count)"; $jumps | Select-Object -First 10 | ForEach-Object { "    $_" }

    # Play/stop segments
    $seg = @(); $prev = $null
    foreach ($b in $blocks) { if ($prev -eq $null -or $b.playing -ne $prev.playing) { $seg += "{0} at block {1} ppq={2}" -f $(if ($b.playing) { 'PLAY' } else { 'STOP' }), $b.block, $b.ppq }; $prev = $b }
    "  transport changes: $($seg.Count)"; $seg | Select-Object -First 30 | ForEach-Object { "    $_" }
}

if ($midi.Count) {
    "  midi kinds: " + (($midi | Group-Object kind | ForEach-Object { "$($_.Name)x$($_.Count)" }) -join ', ')
    "  notes: " + (($midi | Where-Object { $_.kind -eq 'on' } | Group-Object note | Sort-Object { [int]$_.Name } | ForEach-Object { "$($_.Name)x$($_.Count)" }) -join ', ')
    "  channels: " + (($midi | Group-Object ch | ForEach-Object { "$($_.Name)x$($_.Count)" }) -join ', ')
    "  midi without ppq: " + @($midi | Where-Object { $null -eq $_.ppq }).Count
}
