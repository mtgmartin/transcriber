# Phase 8c tests in Ableton Live 11: grids finer than a 32nd note

New in the **Grid** list of the Notation row: **1/64** and **1/128** (the default stays 1/16). The edit bar has **1/64** and **1/128** length buttons (the key **1** is a 64th).
Clips: `t81-fine-run` (piano) and `t82-drum-roll` (drums), written by `tools\live\make-phase8-fixtures.ps1` into the Live project folder.

## 8c.1 Piano (`t81-fine-run`, As played)
- Record it as piano with the default grid (1/16), then choose **1/32**: the run is written as chords of 32nd notes with the warning "... notes were far from the grid".
- Choose **1/64**: bar 1 beat 1 is eight 64th notes C D E F G A B C (four beams), beat 3 is written in 64th notes too (pairs as chords: the 128ths are too fast for this grid), bar 2 beat 1 is two groups of three 64th triplets. The report line says "... notes are shorter than a 32nd note: is the grid of 1/64 too fine for this playing?" (the clip is all very short notes: the question is right).
- Choose **1/128**: beat 3 is eight 128th notes (five beams), the 64th notes and the triplets stay as they were; the rest after the triplets is one 16th rest and one 8th rest, not six 32nd rests.
- Click a 64th note and press **Pitch ▲**; **1/32**, **1/16** change its length (the rest next to it fills the beat). Press **1/128** and **1/64** on a note: the note value is written and the rest after it is right. The key **1** is the 64th.
- Ctrl+A, then **8va ▲**: every note an octave up as one step; Ctrl+Z puts them back.

## 8c.2 Drums (`t82-drum-roll`, Drums)
- With **1/32**: the roll of beat 2-3 is eight 32nd snares; the 64th roll of bar 2 is written as triplet-looking 32nds with the warning "notes were far from the grid / were merged".
- With **1/64**: bar 2 beat 1 is sixteen 64th snares (beamed in one run), then a quarter rest, a crash on beat 3.

## 8c.3 Saving and the PDF
- Ctrl+S, restart Live: the grid 1/64 or 1/128 is still the setting of the take (the Grid list shows it). Export PDF…: the 64th and 128th notes are in the PDF with all their beams.
