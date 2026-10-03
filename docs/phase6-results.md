# Phase 6 results (Ableton Live 11.3.13, Windows 11, 3 Oct 2026)

Build 0.6.0 (CI green), run by Claude in Live with the `tools/live/` scripts (new: `record-slot.ps1` for the tall window
layout). The clips are the new `t31-*` files in Session slots 1-3 of track 7. The plan's gate also says "confirmed by the
user" for the tab; that part is **still open**: the tab below is what the algorithm gives for my test riffs, and I checked it
against the open-string and chord shapes, but you should try your own riffs.

| Test | Result | Evidence |
|---|---|---|
| 6.1 Drums, General MIDI | **Pass** | `t31-drum-groove`, 4 bars: hi-hat x above the staff, snare in the third space, kick in the first space with stems down (feet) and the rest with stems up (hands), "o" above the open hi-hat, crash as x on the ledger line, the ghost snare in brackets in bar 3, the fill (snare, high tom, hi-mid tom, low tom) as sixteenths in bar 4, tempo mark. Line: "Drums · 4 measures · hands and feet in two voices", no warnings. |
| 6.1 Click on drums | **Pass** | "Drums · measure 1 · voice 2 · quarter hit bass drum 1"; "Drums · measure 3 · voice 1 · sixteenth hit snare (ghost note)". |
| 6.2 Unmapped notes | **Pass** | The old piano-range take read as drums said "2 notes are not in the drum map (MIDI note 60, 72) and were left out." |
| 6.2 Map editor | **Pass** | *Edit…* shows every line (note with both names, drum, place, notehead, hands/feet, ghost level). *Learn* + playing the clip took the played note into the line (MIDI 49). Saving with two lines on one note is refused with the note named. After removing High tom and saving: "General MIDI (yours)" is in use, the take is written again and "1 notes are not in the drum map (MIDI note 50)". |
| 6.2 Export / Import | **Pass** | Export wrote `user-1` without note 50 (a file dialog with a pre-filled name needs Ctrl+A before pasting a path, now in the notes). Import read it back ("imported and is in use"). |
| 6.3 Guitar with tab | **Pass** (tab to be confirmed by you) | `t31-guitar-riff`: E major detected, treble clef with 8 below plus a tab staff. Bar 1: 0 0 3 / 0 2 / 2 and the E power chord 0-2-2 on the three lowest strings; bar 2: the open E major chord (0 2 2 1 0 0) and the melody E4 G4 B4 E5 on the top string as 0 3 7 12. |
| 6.3 Bass with tab | **Pass** | `t31-bassline`: bass clef with 8 below, four-line tab, A1 as open A string, F1 as 1st fret on E. |
| 6.3 Click on tab | **Pass** | "Tab · measure 2 · voice 1 · eighth note string 1 fret 3"; a chord gives all its strings and frets. |
| 6.3 Unplayable notes | **Pass** | The drum take read as guitar: "18 notes cannot be played on a 6-string guitar (MIDI notes 40-86, or too many at once) and were left out." |
| 6.4 Switching instrument | **Pass** | The same take read as piano, drums, guitar and bass in turn, each score made again; other takes kept theirs. |
| 6.4 Save, close, reopen | **Pass** | Ctrl+S, Live quit and started again: 11 versions, the bass take showed its bass tab, "General MIDI (yours)" was still the selected map, Instrument kept. |
| 6.5 Confirmed by the user | **Open** | Waiting for your own riffs. |

## Findings (fixed in 0.6.1, not yet seen in Live)

1. **The drum map table is wider than its panel**: the Learn and ✕ buttons of the upper lines were hidden under the
   Versions panel. The table now scrolls sideways inside its panel.
2. "1 notes …" grammar in the warnings; the singular is now used (drums, tab, grid, merged).
3. A click on a tab chord read "half chord string 6 fret 0 string 5 fret 2 …"; now "half chord: string 6 fret 0, string 5 fret 2, …".
4. After switching instrument the line under the score still showed the sentence of the old note; it is cleared when the
   selected note is not in the new score.
5. The unmapped-notes warning showed twice for drums (above the map and in the score line); now once.
6. In the Live window of this size, the window had to be dragged exactly at its corner to resize it (an automation note, in the plan).

## Notes

- The drum notes in the fixture have a closed and an open hi-hat on the same beat (bar 2): both are written, the open one
  displaced beside the closed one. Real drum parts will not usually do that.
- Live restores its window to a small size after being brought forward from the taskbar; maximise it again before using the
  screen coordinates in `docs/BUILD_PLAN.md` §8.
