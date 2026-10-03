# Phase 7a results (Ableton Live 11.3.13, Windows 11, 3 Oct 2026)

Build 0.7.0 (CI green), run by Claude in Live with the practice clip `t41-edit-practice` recorded as piano from slot 9 of
track 7. Two findings were fixed in 0.7.1, which was then checked in Live (rows 7a.1 chords, 7a.4 chords, 7a.5 below).

| Test | Result | Evidence |
|---|---|---|
| 7a.1 Click a note | **Pass** | D4: red, "Right hand · measure 1 · voice 1 · quarter note D4". Chord: whole chord red ("quarter chord C4 E4 G4"); a second click on one of its notes: only that note, line ends "this note: E4". |
| 7a.2 Pitch | **Pass** | Up on the chord's E4 alone: chord C4 F4 G4 ("Pitch changed."). Whole chord Up: C#4 F#4 G#4 with sharps; Ctrl+Up: C#5 F#5 G#5. D4 Up: D#4 with a sharp; Down: back, no sign. |
| 7a.2 Undo / redo | **Pass** | Ctrl+Z three times and Ctrl+Y once: every state came back, the selection stayed; "There is nothing to undo." (in red) when the history is empty. |
| 7a.3 Delete, letter | **Pass** | D4 + Delete: quarter rest. F: a quarter F4 stands there ("Note entered."). Whole note C5 + Delete: measure rest; E: whole note E4 (nearest to the A4 before it). |
| 7a.3 Length | **Pass** | E4 half + 4: eighth note followed by an eighth rest and a quarter rest; + 6: half note again, rests gone. First C4 of bar 2 + 6: half note, "the note after it was taken out to make room"; Undo brings it back. D4 + Dot: dotted quarter, the E4 half note taken out ("Length changed; the note after it was taken out…"); Dot again: quarter D4 and a half rest. |
| 7a.4 Ties | **Pass** | First C4 of bar 2 + T: a tie to the next C4 ("tied to the next note"); T again: gone. Tied again, then the second C4 + Up: C#4, the tie is gone. |
| 7a.4 Chords | **Pass** | 0.7.1: C5 whole note + Add "a 3rd above" twice: chord C5 E5 G5. A click in the hole of the middle note selected just that note ("this note: E5"); Delete: "Note taken out.", chord C5 G5; again: a plain whole note C5. In 0.7.0 the same click selected nothing (finding 1). |
| 7a.5 Edited score | **Pass** | After the first edit "You have edited this score…" and the reading and settings are locked. *Discard my edits* asks "Really discard?", then the score is written from the recording again and the lock is gone. 0.7.1: E4 raised to F#4, Ctrl+S, Live quit and started again: the edited score (F#4 with its sharp) and the "You have edited this score" line are back (laid out in 59 ms), Undo and Redo are dimmed (the history is not kept). |
| 7a.6 Not yet | **Pass** | With a bass take shown, "Editing is for piano scores for now. Guitar, bass and drum scores come later." |

## Findings (fixed in 0.7.1)

1. **A click inside an open notehead (half and whole notes, and the notes of a chord of whole notes) selected nothing**: the
   inside of the head is a hole in the drawing. The page now also tries the boxes of the notes of that page, and takes the
   smallest one under the pointer. (Checked in Live with 0.7.1: a click in the hole of the whole note C5 and of the half note E4 selects them.)
2. **Disabled buttons looked like enabled ones** (the Edit bar before a note is selected, Undo with an empty history). They are
   now dimmed.
3. Test sheet: key **4** is the eighth note, **5** the quarter. The full stop key sent by my automation arrives as Delete
   (the character code of "." is the code of the Delete key), so the Dot button was used; the plugin itself reads the
   key correctly.

## Notes

- Layout and engraving after each edit took about 20 ms for a 3-bar score.
- Selecting chord notes: the first click selects the chord, a second click one of its notes. With an open-headed chord
  that is easier after finding 1.
- Automation: Live was ended with Stop-Process for the restart test, so it asked "Live unexpectedly quit... recover your work?" on the next start (answer No to load the saved set). Close Live through its window next time.
