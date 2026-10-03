# Phase 8e tests in Ableton Live 11: transposition in semitones

New in the Notation row (piano, guitar, bass; not for drums): **Transpose** (semitones, -48 to 48, default 0). For a synth that plays other pitches than the notes it receives: the number
is added to every note before the score is made, so the score shows what is heard. The recording itself is never changed. The value is saved with the take, and the **next recording gets it
too** (it can be chosen before the first recording). A score you have edited keeps its value (the field is locked, like the Grid).

## 8e.1 Before recording (`t83-scale`, piano)
- With no take yet (or before pressing Record) put **2** in Transpose. Record `t83-scale` (a C major scale C4 to C5): the score is **D major** (two sharps, notes D E F# G A B C# D), the field shows 2.
- Change the field on that take to **0**: the score is C major again (made again from the recording); to **-12**: C major an octave lower; to **7**: G major (one sharp).
- Type 100: the field goes back to the last good number (the limit is 48).

## 8e.2 The next take, saving
- Set Transpose to 2 and record again: the new take shows 2, the old take still shows what it had (select it in the Versions list). Ctrl+S, restart Live: both takes keep their values, and the field
  of the next recording is the last one you chose.
- Edit a note, then change Transpose: the field is greyed out ("Discard my edits" first).

## 8e.3 Guitar and bass (`t44-edit-guitar`, `t45-edit-bass`)
- Guitar take, Transpose **-12**: the notes are an octave lower, the tab frets are chosen again (some notes may be below the lowest string: the report line says how many were left out). **+12**: an octave higher, new frets.
- Bass take, **+12**: an octave up. The score has the transposed notes; the tab follows.

## 8e.4 Limits and drums
- Notes pushed out of the range 0-127 are left out; the line under the score says "N notes were out of range after transposing by ... semitones and were left out." (try +60 on `t83-scale`).
- Drums: the Transpose field is not shown (the drum map works on note numbers).
