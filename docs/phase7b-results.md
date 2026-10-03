# Phase 7b results (Ableton Live 11.3.13, Windows 11, 3 Oct 2026)

Build 0.8.0 (CI green), run by Claude in Live with the clip `t42-edit-layout` recorded as piano from slot 9 of track 7. The clip was
detected as F minor, as the test sheet says; the first step was the new **Key** list (C major), which also tested the key change.

| Test | Result | Evidence |
|---|---|---|
| 7b.1 Beam join | **Pass** | E4 selected, **Join**: C D E F under one beam; "This note is joined to the one before it." |
| 7b.1 Beam break / auto | **Pass** | D4 + **Break**: C alone, D E F beamed ("The beam is broken before this note."). **Auto** on D: C D rejoined. Note: the *Join* set on E stays, so C D E F are one beam again (each note keeps its own choice). |
| 7b.1 Stem | **Pass** | C4 + **Down**: the stem points down, the whole beam group turns with it, the line says "eighth note C4, stem down". Key **X**: back to "The stem is left to the program." |
| 7b.2 Spelling | **Pass** | F# of bar 2 + **J**: G flat, the G after it gets a natural sign ("quarter note Gb4"); J again: F sharp. A D + J: "That pitch has only one usual spelling." |
| 7b.2 Tied notes | **Pass** | The two C# half notes of bar 3 tied with T, then J: D flat 5, the tied one follows ("half note Db5, tied to the next note"). |
| 7b.3 Key | **Pass** | From F minor to C major (the Edit-bar list): no signature, the accidentals redone. G major: one sharp in the signature and in the left hand, the F of bar 1 gets a natural sign. E flat major: three flats. A minor: no signature. Ctrl+Z three times: E flat major, G major, C major in turn, the Key list follows each step. |
| 7b.4 Voices | **Pass** | E4 + **Voice 2**: voice 1 has a rest there, voice 2 appears with E4 stem down ("measure 1 · voice 2 · eighth note E4"). **Voice 1** on it: the score is as before. The last note of bar 1 (C5) moved to voice 2. |
| 7b.4 Save, close, reopen | **Pass** | Ctrl+S, Live closed through its window (no recovery question this time) and started again: the edited score is back with its two voices in bar 1, C major in the Key list, Undo grey. |
| 7b.5 Locked | **Pass** | After the first edit the reading and settings are locked; *Discard my edits* is shown. |

## Notes

- The key change spells notes that are not in the new key with sharps for a key of sharps or C, and with flats for a key of flats, so
  in C major the Bb of the clip becomes A sharp (the transcription itself chose Bb). Use **J** on a note to change it.
- Two voices in one bar show the rests of the other voice displaced (Verovio's usual look); it reads correctly but is busy.
  Voice-specific rest placement is a possible later refinement.
- Engraving after each edit: 19-33 ms for these three bars.
- Not tried in Live: moving a note into a place that voice 2 already occupies (unit-tested: nothing changes, "Voice 2 has a note there already.").
