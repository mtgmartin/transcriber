# Phase 7c results (Ableton Live 11.3.13, Windows 11, 3 Oct 2026)

Build 0.9.0 (CI green), run by Claude in Live with `t41-edit-practice` recorded as piano from slot 9 of track 7. One finding, fixed in 0.9.1 (not yet seen in Live).

| Test | Result | Evidence |
|---|---|---|
| 7c.1 Dynamics | **Pass** | The first chord + **mf** in the Dynamic list: "mf" (the music-font glyph) under the staff, "Dynamic mf."; **p**: changes; **Clear marks**: "Markings taken away.", the dynamic is gone. |
| 7c.1 Articulation | **Pass** | D4 + **Staccato**: a dot below the note; **Accent**: the accent replaces it; **Fermata**: a fermata above the staff as well. |
| 7c.2 Slur | **Pass** | First C4 of bar 2, "3 notes on", **Slur**: a slur to the third note after it ("Slur added."). Across the bar line: E4 of bar 1, "2 notes on": a slur into bar 2. On the last note: "There are not enough notes after it." |
| 7c.2 Hairpins | **Pass** | **Cresc.**: an opening hairpin under the staff over the same notes; **Dim.**: it becomes a closing one ("Diminuendo added."). |
| 7c.3 Text | **Pass** | **Text…** opens the dialog; "dolce" + Enter: "dolce" in italics above the note ("Text set."). |
| 7c.3 Tempo | **Pass** | **Tempo…**, "500": refused; 90 and "Andante": "Andante ♩ = 90" above bar 2 ("Tempo mark set."). Opened again with both fields empty: "Tempo mark taken away." Esc closes the dialog without a change. |
| 7c.4 Undo / redo | **Pass** | Ctrl+Z three times: "Undid: Text." (tempo removal, tempo and text undone in turn); Ctrl+Y three times: "Redid: Tempo.", the text is back. |
| 7c.4 Save, close, reopen | **Pass** | Ctrl+S, Live closed through its window and started again: the fermata and accent on D4, both slurs, the diminuendo and "dolce" are all there; the edited-score line is shown. |

## Findings

1. **The Place list of the Text dialog was empty** (the dialog set every field to an empty value, which no option matches). The server then took "above", so
   the result was right, but the list looked broken. Fixed in 0.9.1: a list starts at its first option.
2. The tempo field showed the browser's own message ("Value must be 400 or less", in Croatian) instead of ours. 0.9.1 turns the browser's check
   off for the dialog so that our message shows.

## Notes

- Keys sent while Live has the focus go to Live: Ctrl+Z reached Live's own undo when the pointer had been clicked in the Live window. Click inside the
  plugin window (on its header) before sending keys to the page.
- "Andante" and "dolce" sit close together when a text and a tempo mark are on the same note; they do not overlap.
- Not tried in Live: a hairpin that is replaced by the other kind, a dynamic on a note of a chord on its own, text below the staff (all unit-tested).
