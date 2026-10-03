# Phase 8 results: title, composer, pages and PDF export (Ableton Live 11.3.13, Windows 11, 3 Oct 2026)

Build 0.13.0 (CI green), run by Claude in Live with the drum take of the set, `t43-edit-pages` (piano, As played, 25 measures) and `t44-edit-guitar` (guitar,
As played, 7 measures). The PDFs were saved to `build/pdf/` and opened in Edge. **Two findings, both fixed in the page code (0.13.1, checked in the browser
harness, not yet seen in Live):** a page break is ignored in the A4 pages view and in the PDF, and the Continuous view is tiny after Pages.

| Test | Result | Evidence |
|---|---|---|
| 8.1 Title and composer | **Pass** | `Strofa č & "A" – Šđž` and `M. Ćosić` (typed as Unicode): title centred, composer at the right, all letters correct, in the Pages and the Continuous view. A pitch edit on a guitar note keeps the title. Both fields emptied: the header goes and the music moves up. |
| 8.1 Saved with the take | **Pass** | Ctrl+S, Live closed through its window and started again: the title and composer of the piano take and "Riff" of the guitar take are there (also the 3 pages of the 2-measures-per-line edit). *Discard my edits* keeps the title and composer. |
| 8.2 Pages, size, margins | **Pass** | Large (7 mm) + 20 mm: bigger notation, 4 measures per line; medium + 15 mm: 5 per line. **+** (120%) makes the page wider than the window, **100%** puts it back. |
| 8.3 The PDF | **Pass** | Export PDF…: the Save dialog suggests the title (`Strofa č & A – Šđž.pdf`, the quote mark is not allowed in a file name); "Saved 103 KB to ...". In Edge: one A4 page, title, composer, tempo mark "♩ = 127" (the note is right), notation black on white, DejaVu Serif. Cancel in the dialog: "Not saved." |
| 8.4 Piano, several pages | **Pass** | 25 measures medium/15 mm: 1 page; large/20 mm: 1 page; **2 measures per line (edit)** large: 3 pages (5 lines per page, margins equal on all sides, no staff cut off, the page number "- 2 -" at the top of page 2). |
| 8.4 Guitar | **Pass** | Standard staff and tab on the page, fret numbers readable, title "Riff". |
| 8.4 Drums | **Pass** | Percussion staff with x noteheads, the fermata and text mark of the earlier edits, title and composer. |
| 8.4 New page here | **Fail → fixed** | See finding 1. |
| 0.12.1 fixes | **Pass** | Clicking a chord in the tab selects the chord ("half chord: string 5 fret 0, string 4 fret 2"), a second click on the same place selects the note ("this note: string 4 fret 2", the 2 turns red). Pitch ▲ on it gives fret 1, **Ctrl+Z** brings back fret 2 with the note still selected. |

## Findings

1. **A page break is ignored in the Pages view and in the PDF.** Guitar take, a note of bar 4 + **New page here**: the message says "A new page starts at this
   measure", but the header still says 1 page and bars 1-5 stay on the first line (and "Some lines are broken again ..." appears). Cause (found in the harness with
   the real MEI): `breaks: "smart"` (introduced in 0.10.1 so that long lines are broken again) ignores `<pb/>`; "encoded" keeps it, but then no page is ever
   filled (all the bars after a page break land on one page) and long lines are squeezed. 7d passed in Live on 0.10.0 ("encoded"), and the 0.10.1 recheck did not
   try a page break.
   **Fix** (`withPageBreaks` in `web/score.js`, used by the screen and the PDF): a score with page breaks is laid out once with "smart" (page breaks counted as line breaks),
   the lines of that layout are written as line breaks, a page break is written at each wanted place and wherever a page is full (the number of lines per page is taken from
   that first layout: the fewest on a full page), and the result is laid out with "encoded". Checked in the harness on a 90-measure score: a page break at bar 9 gives
   3 lines on page 1 and 6 on the next pages, two page breaks give 6 pages, the PDF has the same 6 pages; scores without a page break are laid out as before.
   Limit: lines after a page break use the line count of the first layout as the page size (a page is never filled with more lines than were seen to fit, so a page can end a line early).
2. **The Continuous view was a tiny strip after the Pages view** (the Verovio toolkit is kept between renders, and `svgViewBox` and the page margins of the A4 pages
   stayed set). **Fix:** `optionsFor("scroll")` sets `svgViewBox`, `mmOutput` and the margins back (harness: after Pages the Continuous SVG is as wide as the window, no `viewBox`).

## Notes

- Verovio draws "- 2 -" at the top of page 2 and later pages by itself; the plan said "no page numbers": they exist from page 2 (not on page 1). With a page break (the fix above) the numbering follows the pages.
- The test sheet's "two or three pages" for the 24-bar piano take needs a smaller number of measures per line (edit) with the default 5 per line the take is one page.
- While typing a file name into the Save dialog with the Unicode typing tool (`input.ps1 utype`), a click that missed the Export button made the letters arrive as editor keys: the score was edited (undone by *Discard my edits*). The key handling of the page ignores no keys while no dialog or text field is focused, which is as designed; my test method now takes a screenshot of the dialog before typing.
- Not tried in Live: Acrobat (yours), the PDF of a score with a long title (120 characters).
