# Phase 7a tests in Ableton Live 11: editing notes and rhythms

New in the Score panel: the **Edit** bar under the score (Undo, Redo, Pitch, Length, Dot, Tie, Rest, Add a note) and the keys that do the same.
Select a note, chord or rest by clicking it (it turns red), then press a button or a key. Every change is one undo step.
For now only **piano** scores can be edited (guitar, bass and drums come later).

| Key | Does |
|---|---|
| Up / Down | pitch up / down a half step (spelled for the key: sharps in a sharp key, flats in a flat key) |
| Ctrl+Up / Ctrl+Down | octave up / down |
| A to G | on a rest: a note of that letter (the octave nearest the music before it); on a note: changes its letter |
| 2 3 4 5 6 7 | length: 32nd, 16th, eighth, quarter, half, whole |
| . | adds or takes away the dot |
| T | ties the note to the next one of the same pitch (or takes the tie away) |
| Delete | a note or chord becomes a rest; one selected note of a chord is taken out |
| Ctrl+Z, Ctrl+Y | undo, redo |
| Escape | selects nothing |

A longer note takes the rests after it, and then the notes after it (the line says how many). A shorter note leaves a rest.
Notes inside a triplet cannot change length yet; their pitch can be changed.

New test clip `t41-edit-practice` (made by `tools\live\make-phase7-fixtures.ps1`, in `test Project\Transcriber tests`):
3 bars of 4/4 in C major. Right hand: bar 1 a C major chord (quarter), D4 (quarter), E4 (half); bar 2 C4 C4 (quarters),
F4 G4 (eighths), A4 (quarter); bar 3 C5 (whole). Left hand: C3 whole notes. Record it as piano.

## 7a.1 Selecting
- Click the D4: it is red and the line under the Edit bar says "Right hand · measure 1 · voice 1 · quarter note D4".
- Click the chord: the whole chord is red ("quarter chord C4 E4 G4"). Click one of its notes again: only that note is red and the line ends "this note: E4".
- Click an empty place: nothing is selected, the edit buttons are grey.

## 7a.2 Pitch
- Select D4, press **Up**: it becomes D#4 with a sharp sign. Press **Down**: D4 again, no sign. **Ctrl+Up**: D5 (octave).
- Select the chord's E4 alone (click chord, click E4) and press Up: the chord is C4 F4 G4. With the whole chord selected, Up moves all three.
- Move a note of the chord onto another one of its notes: "That pitch is already in the chord."
- Press **Ctrl+Z** several times: everything comes back in order. **Ctrl+Y** does it again.

## 7a.3 Rests and notes
- Select D4, press **Delete**: it becomes a quarter rest. Press **F**: a quarter F4 stands there again (nearest to the music before).
- Select the half note E4 (click anywhere on it, also inside the open head), press **5**: it is a quarter, followed by a quarter rest; **4** gives an eighth. Press **6**: a half note again, the rest is gone.
- Select the first C4 of bar 2, press **6** (half note): it takes the second C4 (the line says the note after it was taken out). Undo brings it back.
- Select the quarter D4, press **Dot** (the key is the full stop): dotted quarter. It runs into the half note E4, which is taken out (the line says so), and an eighth rest is left. Press Dot again: the dot is gone, the rests join. Undo twice brings E4 back.
- Select the whole note C5 in bar 3, press **Delete**: the bar shows a measure rest. Press **E**: a whole note E (nearest to the music before).

## 7a.4 Ties and chords
- Select the first C4 of bar 2, press **T**: a tie curves to the next C4. Press **T** again: it is gone.
- Tie it again, then select the second C4 and press **Up**: the tie is gone too (the pitches differ).
- Select the C5 (bar 3), choose "a 3rd above" in the Edit bar and press **Add**: it is a chord C5 E5. Add "a 5th above": a third note. Select one of the notes and press Delete: it leaves the chord.
- Press Delete on one note of a two-note chord: it is a plain note again.

## 7a.5 An edited score
- After the first edit the line "You have edited this score..." appears; the Reading buttons, Grid, Instrument, Key and the other notation settings are grey.
- Click **Discard my edits**, then **Really discard?**: the score is written from the recording again, the settings are usable.
- Edit again, press Ctrl+S in Live, close Live and open the set again: the edited score is there, Undo and Redo are grey (the history is not kept).

## 7a.6 Not yet
- Switch the instrument of a take to Guitar: the Edit bar buttons stay grey and the line says editing is for piano scores for now.
- Select a note inside a triplet (if there is one) and press 4: "The length of a note inside a triplet cannot be changed yet." Its pitch can be changed.

## Speed
- In a score of 90 bars an edit shows within a second ("laid out in ... ms" under the Score toolbar). Report anything slower.
