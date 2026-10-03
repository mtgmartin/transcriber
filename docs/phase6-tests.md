# Phase 6 tests in Ableton Live 11

New in the Capture panel: the **Instrument** choice in the Notation row (Piano, Drums, Guitar, Bass) and, for drums,
the **Drum map**. A take is written for the instrument that is chosen; it can be changed afterwards and the score is made
again from the same recording. A new recording gets the instrument that was chosen last.

New test clips (in `test Project\Transcriber tests`, made by `tools\live\make-phase6-fixtures.ps1`):
`t31-drum-groove` (4 bars of rock groove with an open hi-hat, a ghost snare, a crash and a tom fill, General MIDI notes),
`t31-guitar-riff` (E2 line, power chord, open E chord, melody), `t31-bassline` (A1-based line, then F).
`t23-drums-3min` is the long drum part.

## 6.1 Drums with General MIDI

Put the Transcriber on a track that receives the drum clip as plain MIDI (a MIDI track with the clip, or a Drum Rack track with
the Transcriber in an Instrument Rack chain). Choose **Drums** and record `t31-drum-groove`.
- Expect: one five-line staff with a percussion clef. Hi-hat as x above the staff, snare in the third space, kick in the
  first space (stems down, the feet), hi-hat/snare/crash/toms with stems up (the hands). The open hi-hat has an **o** above it,
  the crash is an x on the ledger line above the staff, the ghost snare in bar 3 is in brackets, the fill in bar 4 runs
  snare, high tom, hi-mid tom, low tom as sixteenths.
- *Score as text* shows the drum names and `( )` around the ghost note. No warnings.
- Click a note: "Drums · measure 1 · voice 2 · quarter hit bass drum 1" and similar.

## 6.2 A Drum Rack with a map of your own

Record the drum part from a **Drum Rack** whose pads are not on the General MIDI notes (the pad notes start at C1 = MIDI 36 in
Live, but a rack can be arranged differently).
- Expect: notes that are not in the map are listed in a yellow line ("N notes are not in the drum map (MIDI note …) and were left out").
- Press **Edit…**. Press play in Live, click **Learn** on a line and hit a pad: the note you play is taken (the last note
  played is shown too, with both the usual name and the name Live shows). Change the place, the notehead and the voice; **Add a drum**
  adds a line; press **Save map**. The score is made again at once. Saving a built-in map makes a copy ("My kit").
- **Export…** writes the map as a `.json` file; **Import…** reads one (also one written by hand).
- The map belongs to the take: switching to another take keeps its own copy; new takes use the map that is selected.
- Delete (only for your own maps).

## 6.3 Guitar and bass with tab

Choose **Guitar**, record `t31-guitar-riff`.
- Expect: a treble staff with the 8 below the clef and a tablature staff under it, joined by a bracket. The first bar reads
  6:0 6:0 6:3 5:0 5:2 and the E power chord 0-2-2 on the three lowest strings; the second bar is the open E major chord
  (0 2 2 1 0 0) and the melody E4 G4 B4 E5 on the top string.
- Choose **Bass**, record `t31-bassline`: bass clef with the 8 below and a four-line tab; A1 is the open A string.
- Click a fret number: the line under the score says e.g. "Tab · measure 1 · voice 1 · eighth note string 6 fret 0".
- A note that cannot be played (below the lowest string, above the 22nd fret) is listed in a warning and left out.

## 6.4 Changing the instrument of a take

Select a take and switch the instrument: the score is made again from the same recording (piano ↔ drums ↔ guitar ↔ bass).
Other takes keep theirs. Save the set, close and reopen Live: instruments, maps and scores come back.

## 6.5 Confirmed by the user

The plan's gate: "a guitar riff and a bassline give standard notation plus playable tab, confirmed by the user".
Check the tab of your own riffs against what you would play on a guitar and report what is wrong.
