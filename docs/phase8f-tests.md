# Phase 8f tests in Ableton Live 11: tunings of a guitar or bass

New in the Notation row for guitar and bass: **Tuning**. *Automatic* (the default) writes the take in the standard tuning when it can play every note, and otherwise in the smallest change that can:
the lowest string down (Drop D), an extra low string (a 7-string guitar, a 5-string bass), or all strings tuned down. A list has the usual tunings (Drop D, Double drop D, DADGAD, Open G, Open D,
Open E, half step down, D standard, Drop C, C standard, 7 and 8 strings; for the bass Drop D, half and whole step down, 5 and 6 strings) and **My own...** takes the notes of the strings from the
lowest as names or MIDI numbers (`D2 A2 D3 G3 B3 D4`, `C#2 Db3`, `38 45 50`). A tuning other than the standard one is written under the tab at the start ("Tuning: D A D G B E").
When the tab changes tuning the notation does not change, only the strings and frets. After you have edited the score the choice is made as an **edit** (undoable); Automatic is not possible then.
Clips: `t84-low-guitar`, `t85-low-bass` (and `t44-edit-guitar`, `t45-edit-bass`).

## 8f.1 Automatic (guitar, `t84-low-guitar`, Guitar, As played)
- Record it and set Instrument to Guitar. Tuning: **Automatic**. Bar 2 has a B1: the line under the score says "Tuned to 7 strings (B standard) so that all notes can be played." and "tuning: 7 strings (B standard)"; the tab has seven lines, the first note of bar 2 is `0` on the lowest line; "Tuning: B E A D G B E" is written under the tab. No note is left out.
- Choose **Standard**: the B1 and some other notes are left out and the report says "... cannot be played on a 6-string guitar ...". **Drop D**: the D2 notes are open on the lowest string, the B1 is left out. Back to **Automatic**: the 7 strings again.
- Record `t44-edit-guitar` (everything fits): Automatic = Standard, no words under the tab and no warning.

## 8f.2 Your own tuning
- **My own...**: type `D2 A2 D3 G3 B3 D4`, Enter: the list says "Double drop D", the notes of bar 1 are at other frets. Type `D2 A2 D3` (too few): the message "A guitar has 4 to 8 strings, not 3." and nothing changes. Type `D2 H2`: the message names the wrong word. Type strings that do not rise: the message says so.

## 8f.3 Edited scores
- On a take with edits (any pitch change) choose **Drop D**: the tab is written again for those strings in one undo step ("Change tuning": Ctrl+Z puts the strings back). The notation does not change. Choose **Automatic**: the message "Automatic needs the whole take: discard your edits first, or choose a tuning."
- A tuning that cannot play a note of the take (for the B1 take, Standard) is refused: "That note cannot be played on a guitar (out of its range ...)" and nothing changes.
- After a new tuning the string buttons (Tab string ▲▼) and the pitch keys work as before.

## 8f.4 Bass (`t85-low-bass`, Bass)
- Automatic: the take needs a B0 in bar 2, so Automatic writes a **5-string bass** (B E A D G) and says so. Choose **Drop D** (4 strings): bar 1 plays, the B0 is left out. **6 strings** adds the high C.

## 8f.5 Saving and the PDF
- Ctrl+S, restart Live: the tuning of the take (also the one of an edited score) is still there. Export PDF…: the seven lines of the tab and the words "Tuning: ..." are in the PDF.
