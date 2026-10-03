# Phase 7c tests in Ableton Live 11: markings and text

New in the Edit bar (a third row): **Dynamic** (list), **Articulation** (Staccato, Accent, Tenuto, Marcato, Fermata), **Slur / hairpin to**
(how many notes on) with **Slur**, **Cresc.**, **Dim.**, **Text…**, **Tempo…** and **Clear marks**. They act on the selected note or chord
(piano scores only). Every change is one undo step.

- **Dynamic:** pp, p, mp, mf, f, ff and others, written under the staff. The same one again, or *Clear marks*, takes it away.
- **Articulation:** one of staccato, accent, tenuto, marcato per note (another replaces it; the same takes it away); the fermata is separate.
- **Slur / Cresc. / Dim.:** from the selected note to the 1st to 8th note after it in the same voice (rests are skipped, bars are crossed).
  Doing it again on the same note takes it away. A slur and a hairpin can start on the same note, a crescendo replaces a diminuendo.
- **Text…:** opens a small dialog: text up to 80 characters, above or below the staff. Empty text takes it away.
- **Tempo…:** a dialog for beats per minute (20 to 400) and/or text such as "Andante", written above the first staff at the selected note.
  Both empty takes the mark away at that note. The first mark of the take (the tempo of the recording) can be changed the same way.
- The line under the Edit bar says what is on the selected note, e.g. "marks: mf, staccato, slur starts here".

Use the practice clip `t41-edit-practice` (piano, chord and quarters in bar 1, C C F G A in bar 2, whole note in bar 3).

## 7c.1 Dynamics and articulation
- Select the first chord, choose **mf** in the Dynamic list: "mf" appears under it, the line says "marks: mf". Choose **p**: it changes. **Clear marks**: gone.
- Select D4, press **Staccato**: a dot appears. Press **Accent**: the accent replaces it. Press **Fermata**: a fermata appears above it as well.
- Select a rest (none in the clip: delete a note first): the Dynamic list and the buttons say a dynamic belongs to a note.

## 7c.2 Slurs and hairpins
- Select the first C4 of bar 2, set "3 notes on", press **Slur**: a slur from it to the third note after it. Press **Slur** again: gone.
- Press **Cresc.**: a crescendo hairpin under the staff over the same notes. Press **Dim.**: it becomes a diminuendo. Press **Dim.** again: gone.
- A slur across the bar line: select the last note of bar 1 (E4), "2 notes on", Slur.
- Select the whole note at the end and press Slur: "There are not enough notes after it."

## 7c.3 Text and tempo
- Select a note, **Text…**, type "dolce", place below, OK: "dolce" under the note. Open it again, empty text: gone.
- Select the first note of bar 2, **Tempo…**, 90 and "Andante": the mark appears above bar 2. Type 500: "The tempo can be 20 to 400." in the dialog.
- **Tempo…** at the same note with both empty: the mark is gone. Press Esc or Cancel: the dialog closes without a change.

## 7c.4 Undo, edited score, saving
- Ctrl+Z walks back through every marking in order; Ctrl+Y does them again.
- After Ctrl+S, quitting and starting Live: dynamics, slur, text and tempo marks are still there.
- *Discard my edits* writes the first transcription again, without any marking.

## Not in 7c
Guitar, bass and drum scores, hairpins that start in another voice than they end, title and composer on the page (they come with the PDF in Phase 8).
