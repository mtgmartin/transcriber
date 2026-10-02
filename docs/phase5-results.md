# Phase 5 results (Ableton Live 11.3.13, Windows 11, 2 Oct 2026)

Build 0.5.0 (CI green), run by Claude in Live with the `tools/live/` scripts. The plugin window was made taller by
dragging its corner (the Score panel is now at the top, so the Capture buttons are lower than in the older notes:
Record is at about (1292, 782) with that window size). The reference clips from the user have not arrived yet.

**Gate: passed** ("a recorded piano take shows correctly in Live; 200 bars re-render in under 1 s"). The 200-bar
timing was measured in the browser pane with a copy of the page (0.45 s to lay out 200 bars); in Live the real
take of 93 bars laid out in 125-285 ms.

| Test | Result | Evidence |
|---|---|---|
| 5.1 A take as notation | **Pass** | The 4-bar clip: 4 measures on a grand staff, C# minor (4 sharps), 4/4, "quarter note = 127", the dotted-quarter rest, eighth and sixteenth in the right hand, bass quarter, quarter rest and half rest in the left; a natural sign on D and G where the key signature differs. Same as *Score as text*. |
| 5.2 Three-minute piano | **Pass** | 93 measures in 12 pages, laid out in 283 ms the first time and 125-180 ms afterwards. Chords have one stem, beams stay in the beat, triplets (Triplets on) are bracketed with a 3, scrolling draws the pages. |
| 5.3 Clicking | **Pass** | Note: red, "Right hand · measure 1 · voice 1 · eighth note C#4". Rest: "dotted quarter rest"; half rest: "Left hand · measure 1 · voice 1 · half rest". Chord: "Left hand · measure 1 · voice 1 · quarter chord C4 E4 G4". Click on the white page clears the selection. Triplet and tie sentences: unit test only (`mei: triplets and ties in the click sentence`). |
| 5.4 Pages / Continuous | **Pass** | Pages gives 6 pages for the 93 bars, Continuous 12 pieces; the choice is kept. |
| 5.4 Zoom | **Pass** | + to 140 %: 43 pieces, lines re-broken, laid out in 177 ms; − twice and *100 %* return to 100 %. |
| 5.4 Resize | **Pass** | Window made narrower: 2 measures per line; wider again: the lines go back. |
| 5.4 Settings | **Pass** | Triplets on: the triplet groups appear, laid out in 176 ms, the scroll position stayed at measure 8. |
| 5.4 Another version | **Pass** | Selecting the 4-bar take showed it from the top. |
| 5.4 While recording | **Pass** | The score of the selected version stayed on screen ("The score is made when the recording stops."); the new take's score appeared when the recording stopped (1 measure, F minor). |
| 5.5 Save, close, reopen | **Pass** | Ctrl+S, Live quit and started again: 8 versions, the selected take's score was shown again (in 47 ms of layout). |
| 5.6 Reference clips | **Waiting** | |

## Findings

1. **The Score panel stays empty for a few seconds after the window is opened** ("No score yet" for about 3 s on a
   cold start, then the score appears): Verovio's WASM has to start first. Later openings in the same session are
   quicker. Not changed; worth a "Starting the engraver…" text if it annoys.
2. **Continuous had white gaps** (a piece holds whole systems, so the end of each piece was blank). The pieces are
   taller in 0.5.1 (1.4 × the width instead of 0.6 ×), which makes the gaps rarer; not yet seen in Live.
3. The page's default Live window is now 1100 x 860.
