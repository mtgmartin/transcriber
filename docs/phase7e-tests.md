# Phase 7e tests in Ableton Live 11: editing guitar and bass scores

Guitar and bass scores can be edited now. The standard staff is the one that is edited; the **tab staff follows**: the same notes, with the strings and frets
worked out again for the notes that changed. Everything of the piano editor works on these scores (pitch, length, dots, delete, add a note above, ties, spelling,
key, stems, beams, markings, text, tempo, layout), except **Voice** (a guitar or bass score has one voice).

- You can click a note in the **standard staff or in the tab**. A click in the tab edits the note it stands for, and the tab note stays selected.
- New: **Tab string ▲ / ▼** (second edit row): plays the selected *tab* note on the next higher / lower string (the same pitch, another fret). The standard staff does not change.
  A note you moved by hand stays where you put it when you edit other notes. A chord: click the chord, click again on one of its notes, then move it
  (the string must be free).
- A note that **cannot be played** (below the lowest string, above the 22nd fret, or a chord with no way to hold it) is refused: "That note cannot be played on a guitar ...".
- Marks (dynamics, slurs, text) are on the standard staff only. A key change changes the spelling of the standard staff; the tab does not change.
- Drum scores cannot be edited yet.

New test clips in `test Project\Transcriber tests` (made by `tools\live\make-phase7-fixtures.ps1`):
- `t44-edit-guitar`: 3 bars. Bar 1: E2 A2 D3 G3 (open strings); bar 2: A2+E3 chord (half), B3, C4; bar 3: eight eighths E3 G3 A3 B3 A3 G3 E3 D3.
  Record it (about 12 s), then choose **Instrument: Guitar**. The tab reads: `0 0 0 0 | 0/2 chord, 0, 1 | ...`.
- `t45-edit-bass`: 3 bars for a bass (E1 A1 D2 G2; E1 half, G1, A1; eighths E1 E1 G1 A1 B1 A1 G1 E1). Choose **Instrument: Bass**.

## 7e.1 Notation staff
- Select the first note (E2): press **Down** (or the Pitch ▼ button): "That note cannot be played on a guitar ..." (it is the lowest note of the instrument).
  Press **Up**: F2, and the tab shows fret 1 on the lowest string. Ctrl+Z.
- Select the third note (D3), **Delete**: a rest in both staves. Ctrl+Z.

## 7e.2 Click in the tab
- Click the second tab note (open A string, "0"): press **Up**: A#2 in the standard staff, "1" on the same string in the tab; the tab note is still selected.
  Ctrl+Z.
- **Tab string ▼**: the note moves to the lowest string, fret 5 ("5" on the lowest line); the standard staff does not change. **Tab string ▲**: back.
  Click the first tab note (E2) and press **▲**: "That note cannot be played on that string" (E2 does not exist on the A string); **▼**: "There is no lower string".
- Click the standard-staff note and press Tab string ▼: "Click a note in the tab staff first".

## 7e.3 Chord
- Click the chord of bar 2 in the tab, press **Tab string ▼**: "Click one note of the chord (click it again) ...". Click it again (one note), **Tab string ▼**: that note
  moves down a string; the other note stays.
- Select the B3 of bar 2, **Add note**: "a 3rd above": a chord B3+D4; the tab shows two numbers on two strings.

## 7e.4 Marks, key, layout
- Select a note, Dynamic **mf**: "mf" under the standard staff only. **Slur** with 2 notes on: a slur in the standard staff only.
- **Voice 2**: "A guitar or bass score has one voice."
- Key list: **B♭ major**: the standard staff is spelled with flats; the tab numbers are the same.
- **Measures per line** 2: the lines hold two measures, standard staff and tab together.

## 7e.5 Undo, saving, bass
- Ctrl+Z walks back through everything; Ctrl+Y does it again. Ctrl+S, close Live, open it: the edited guitar score is as it was left.
- Record `t45-edit-bass` and choose Bass: the lowest E1 cannot go down; A1 (second note) with **Tab string ▼** goes to the lowest string, fret 5.
- *Discard my edits* brings the first transcription back.
