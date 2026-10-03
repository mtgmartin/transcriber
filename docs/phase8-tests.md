# Phase 8 tests in Ableton Live 11: title, composer, pages and PDF export

New under the Score toolbar (a row of its own):
- **Title** and **Composer**: written above the score, on the page and in the PDF (title centred, composer at the right, in the font DejaVu Serif, which has the
  letters of every Central European language). Saved with the take. Empty: no header, no space for one.
- **Notation size** (small / medium / large: a staff 5, 6 or 7 mm high) and **Margins** (10, 15, 20 mm): they set the A4 pages.
- **Export PDF…**: asks where to save the file (the title, or the name of the take, is suggested as the file name).
- The **Pages** view is now the A4 page of the PDF (the same engraving; the window only makes it larger or smaller: 100% is the page as wide as the window),
  so the PDF is what you see there. **Continuous** is one long page as before, with the title at the top.

## 8.1 Title and composer
- Record `t41-edit-practice` as piano. Click *Title*, type `Strofa č & "A" – Šđž`, press Enter; *Composer*: `M. Ćosić`. The title is above the first system (centred) and the composer
  at the right, in both views. The letters č, Š, đ, ž, Ć show correctly.
- Select a note, edit something (Pitch ▲): the title stays. Empty both fields: the header goes and the music moves up.
- Ctrl+S, close Live, open it again: the title and composer are still there (also after *Discard my edits*).

## 8.2 Pages, size and margins
- *Pages*: A4 sheets with white borders. **Notation size**: large makes the notation bigger and the lines hold fewer measures; small the other way. **Margins** 20 mm: wider borders.
- + and − change the size of the page on the screen (not the PDF).

## 8.3 The PDF
- Press **Export PDF…**: the line under the toolbar says "Making the PDF: page 1 of n…" and then "The PDF has n pages ... Choose where to save it." The file dialog suggests the title as the name.
- Save it in Documents, open it in Edge (and in Acrobat, if you have it): the same page as on the screen: title, composer, notation, tempo marks ("♩ = 120"), dynamics, slurs, text.
  The notation is black on white (not the dark page colours), the letters are the same font as on the screen.
- Press Export, then *Cancel* in the dialog: "Not saved."

## 8.4 Every instrument, several pages
- Piano take of 24 bars (`t43-edit-pages`, As played): the PDF has two or three pages, margins equal on all sides, no staff cut off at the bottom.
- Guitar (`t44-edit-guitar`, Instrument: Guitar): standard staff and tab on the page, tab numbers readable. Drums (`t31-drum-groove`, Instrument: Drums): the percussion staff with x noteheads.
- A score with *New page here* (7d): the page break is in the PDF too.
