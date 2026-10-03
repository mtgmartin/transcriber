# Phase 7d results (Ableton Live 11.3.13, Windows 11, 3 Oct 2026)

Build 0.10.0 (CI green), run by Claude in Live with `t43-edit-pages` recorded as piano from slot 9 of track 7 (read As played: 28 measures, 3 per line at the
start). One real finding (long lines), fixed in 0.10.1 (checked in the browser harness, not yet seen in Live).

| Test | Result | Evidence |
|---|---|---|
| 7d.1 New line here | **Partly** | Note of bar 5 + **New line here**: bar 5 starts a line ("A new line starts at this measure."), but 0.10.0 then put bars 5–28 on one single line, squeezed until the notes overlap (finding 1). Again: "The break is taken away.", back to the first layout. |
| 7d.1 First measure | **Pass** | Bar 1 + **New line here**: "The first measure always starts the first line." |
| 7d.2 Measures per line | **Pass, with a finding** | **4**: "A line holds 4 measures.", every line has 4 measures; **8**: eight measures per line, close together; the warning that the test sheet expected did not come (finding 1). |
| 7d.3 Pages | **Pass** | In *Pages*, note of bar 9 + **New page here**: "A new page starts at this measure", the header says 2 pages; **No break**: 1 page again. A page break at bar 17, then **4** per line: still 2 pages, the page break kept. |
| 7d.4 Spacing | **Pass** | **Roomy**: more space between the right and the left hand; again: "The spacing is that already."; **Tight**: closer together. |
| 7d.5 Undo / redo | **Pass** | Ctrl+Z (after a click on the plugin header): "Undid: Spacing."; Ctrl+Y: "Redid: Spacing." |
| 7d.5 Save, close, reopen | **Pass** | Ctrl+S, Live closed through its window, started again: 28 measures, 2 pages, 4 per line and the tight spacing are all still there. |
| 7d.5 Discard my edits | **Pass** | Two clicks: back to the first transcription (3 per line, 4 pages, no breaks). |

## Findings

1. **Lines between breaks were squeezed, not broken.** In 0.10.0 the page used Verovio's `encoded` mode as soon as the score had one break, and then Verovio
   lays out every line between two breaks as one line, however many measures it has. A single break at bar 5 gave bars 5–28 on one line; 8 measures per line were
   narrow. My "line is longer than the window" check never fired, because Verovio squeezes the line to the page width instead of letting it run over.
   - Fix in 0.10.1: `breaks: "smart"` with `breaksSmartSb: 0`. The breaks of the score are always kept, and Verovio breaks a line again where it does not fit
     (harness, 28 bars with a break at bar 5: encoded gave lines of 4 and 24 measures, smart with threshold 0 gives 3, 1, 3, 4, 4, 4, 3, 4, 2). Without breaks it is the same as "auto".
   - The line under the Score toolbar now says "Some lines are broken again because they do not fit the window: use fewer measures per line, or a smaller zoom"
     when a line does not start at a break of the score (checked in the harness: shown for 8 per line, not shown for the 8-measure `breaks` sample).
   - So "Measures per line" means "at most this many" when the window is narrow; the number of measures that fit depends on the window width and the zoom
     (the PDF in Phase 8 will use a fixed page).
2. Test slip, not a plugin problem: a click at the wrong place hit the activator button of track 8 (the project was saved with the track off). Found after the restart
   and switched on again; the project was saved again with the track on.

## Notes

- The bar numbers of the test sheet change with the window width (the program breaks lines before the first break is used); this run used bars 9 and 17 for the
  page breaks, which are line starts with 4 per line.
- Not tried in Live: a break in a score with two voices or a pickup bar (unit-tested), the PDF (Phase 8).
