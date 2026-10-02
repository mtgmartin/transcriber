# Phase 4 tests in Ableton Live 11

The logic is covered by the unit tests (`tests/core/test_transcribe.cpp`: every stage alone, 20 golden scores, the fixture
`.mid` files and 150 random clips). These Live tests check that the page shows the right thing for real recordings.
There is no sheet-music view yet (Phase 5), so the score is read as text: open **Score as text** in the Capture panel.

How to read it: `S1` is the right hand (treble), `S2` the left hand (bass), `v1`/`v2` are voices, `/4` a quarter note,
`/8.` a dotted eighth, `r` a rest, `R` a measure rest, `[C4 E4 G4]/2` a chord, `~` a tie to the next note, `( )` a beam,
`< >` a triplet, `!` an accidental that is shown.

## 4.1 A simple loop

Record `t21-loop4` (Session View, as in the Phase 2 tests).
- Expect: key line (a key, "4 measures"), "Score as text" with 4 measures, each bar a bass note in S2 and three notes in S1,
  rests of the right size. No warnings.

## 4.2 The three-minute piano part

Record `t23-piano-3min` once in the Arrangement (As played).
- Expect: 90 measures, up to 2–3 voices, no warnings, bars 8, 16, 24 ... with a triplet figure in `< >`.

## 4.3 Settings change the score

With a take selected:
- **Grid** 1/8 instead of 1/16: the 16th notes of the part are moved to eighths (a line says how many notes were moved).
- **Triplets** off: triplet figures become straight notes.
- **Hands split at** 72: more notes go to the left hand.
- **Key** set to a major or minor key: the key line says "set by you" and the spelling follows (sharps or flats).
- **Pickup bar** off/on for a clip whose first note is on the last beat of a bar.
- Switching *One loop / As played* makes the score again.

## 4.4 Saving

Save the set, close and reopen Live: the versions and their settings are back, and "Score as text" is the same
(an unedited score is made again from the recording when the set is loaded; the saved size stays small).

## 4.5 Reference clips

When the reference clips arrive: record each, compare the score text with the notation you expect, and list the differences
in `docs/phase4-results.md`. The unit tests are updated from them.
