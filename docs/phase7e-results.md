# Phase 7e results: guitar and bass (Ableton Live 11.3.13, Windows 11, 3 Oct 2026)

Build 0.12.0 (CI green), run by Claude in Live with `t44-edit-guitar` (recorded as piano from slot 9 of track 7, then Instrument: Guitar, read As played) and `t45-edit-bass`.
Two findings, both fixed in 0.12.1 (checked in the browser harness and the unit tests, not yet seen in Live).

| Test | Result | Evidence |
|---|---|---|
| Edit bar for the instrument | **Pass** | Guitar: no Voice buttons, the **Tab string** ▲▼ buttons are there. |
| 7e.1 Lowest note down | **Pass** | E2 + Pitch ▼: "That note cannot be played on a guitar (out of its range, or not possible together with the other notes)." |
| 7e.1 Pitch up | **Pass** | E2 + Pitch ▲: F2 in the notation, "1" on the lowest tab line, "Pitch changed."; Ctrl+Z ("Undid: Change pitch.") puts it back. |
| 7e.1 Delete | **Pass** | D3 + Rest: a rest in the notation and in the tab ("Made a rest."). |
| 7e.2 Click in the tab | **Pass** | The second tab note (open A string) + Pitch ▲: A#2 (sharp) in the notation, "1" on the A string, the tab note stays selected (red; "Tab · measure 1 · quarter note string 5 fret 1"). |
| 7e.2 Tab string | **Pass** | ▼: "On string 6, fret 5." (a "5" on the lowest line, the notation staff unchanged); ▲: "On string 5, fret 0." E2 ▲: "That note cannot be played on that string (it would be fret -5)."; ▼: "There is no lower string."; a note of the notation staff: "Click a note in the tab staff first ...". |
| 7e.3 Chord | **Partly** | The whole chord of bar 2 + ▼: "Click one note of the chord (click it again) ...". But the second click on a note of the chord in the **tab** did not select that note (finding 1). B3 + Add a note (3rd above): a chord B3+D4, "0" and "7" on two strings of the tab. |
| 7e.4 Marks | **Pass** | Dynamic mf: "mf" under the notation staff only; Slur: a slur under the first two notes, notation staff only ("slur starts here"). |
| 7e.4 Key | **Pass** | B♭ major: flats in the notation, the tab numbers unchanged. |
| 7e.4 Layout | **Pass** | Measures per line 2: "A line holds 2 measures.", two measures per line in both staves. |
| 7e.4 Voice | **Not tried** | The Voice buttons are hidden for guitar and bass (unit-tested: "A guitar or bass score has one voice."). |
| 7e.5 Saving | **Pass** | Ctrl+S, Live closed through its window and started again: the edited guitar score (B♭, 2 per line, "edited by you") is as it was left. |
| 7e.5 Bass | **Pass** | `t45-edit-bass` with Instrument: Bass: a 4-line tab; E1 + Pitch ▼: "That note cannot be played on a bass ..."; A1 (tab) + ▼: "On string 4, fret 5." |

## Findings

1. **Second click on a note of a chord in the tab did not select the note** (the check in `score.js` looked at standard-staff chords only). Fixed in 0.12.1; harness: a chord selected, then a click on its note, selects the note.
2. **Undoing the first edit lost the selection** in a guitar score: the first edit renewed the ids of the tab (to the id of the notation node plus "-t"), and undo brought the old ids back. Fixed in 0.12.1: the tab ids are of that form from the start (`deriveTabIds` in `Fretted.cpp`), so the ids never change; a unit test checks the ids before the edit, after it and after its undo.

## Notes

- A click that lands beside a list and Down/Up keys edit the selected note (Down = pitch -1): I did that once by mistake and undid it with Ctrl+Z. The arrow keys act on the selection wherever the pointer was, as intended for the editor.
- Not tried in Live: the string move of one note of a chord (finding 1 blocked it; unit-tested), a chord that cannot be played (unit-tested).
