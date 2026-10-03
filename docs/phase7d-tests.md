# Phase 7d tests in Ableton Live 11: page layout

New in the Edit bar (a fourth row, **Layout**): **New line here**, **New page here**, **No break** (for the measure of the selected note),
**Measures per line** (a list) and **Spacing** (tight, normal, roomy). Piano scores only, like the rest of the editor; every change is one undo step.

- A **line break** or **page break** is stored on the measure it comes before; the first measure cannot have one. The same button again, or *No break*, takes it away.
- **Measures per line** puts a line break before every n-th measure (n = 1 to 8) and takes away the other line breaks; page breaks stay.
  "the program decides" takes all line breaks away.
- **Spacing** changes the distance between the lines of music and between the two staves of a line (Tight / Normal / Roomy).
- Your breaks are always kept. Between them the program still breaks a line that does not fit the window (0.10.0 left such a line as one squeezed line);
  then the line under the Score toolbar says "Some lines are broken again because they do not fit the window: use fewer measures per line, or a smaller zoom".
  Without breaks the program breaks the lines as before.
- *Pages* view shows the page breaks (a new sheet), *Continuous* keeps one long page.

New test clip `t43-edit-pages` (made by `tools\live\make-phase7-fixtures.ps1`, in `test Project\Transcriber tests`): 24 bars of 4/4, a C major scale in
quarter notes up and down in the right hand, C3 and G2 half notes in the left. Record it as piano (about 50 seconds; it loops in the Session slot).

## 7d.1 Line breaks
- In *Continuous* view note how many measures the program puts on a line. Select a note in bar 5 and press **New line here**: bar 5 starts a new line
  and the lines before it are broken as before (the lines after it too, until the next break). Press it again: the break is gone.
- Select a note in bar 1 and press **New line here**: "The first measure always starts the first line."

## 7d.2 Measures per line
- Choose **4** in *Measures per line*: every line holds 4 measures (6 lines). Choose **3**: 8 lines. Choose "the program decides": the lines are as at the start.
- Choose **8** with the window narrow (or the zoom at 200%): the lines that do not hold 8 measures are broken again, and the line under the Score toolbar says so.
  Press Ctrl+Z, or choose a smaller number: the note is gone.

## 7d.3 Pages
- Switch to *Pages*. Select a note in bar 9, press **New page here**: bar 9 starts a new sheet. **No break** takes it away.
- Choose **4** per line again; a page break at bar 13 keeps its place (the measures-per-line list does not touch page breaks).

## 7d.4 Spacing
- Choose **Roomy**: more space between the lines and between the right and left hand staves. **Tight**: less. **Normal**: back to the standard.
- Choosing the same spacing again changes nothing ("The spacing is that already.").

## 7d.5 Undo, saving
- Ctrl+Z walks back through the layout steps (a list choice is one step); Ctrl+Y does them again.
- Ctrl+S, quit and start Live: the breaks and the spacing are still there; *Discard my edits* brings the first transcription back, without breaks.
