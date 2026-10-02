# Phase 5 tests in Ableton Live 11

The score is now shown as sheet music at the top of the plugin window (the *Score* panel). The text form is still
under *Score as text* in the Capture panel; both come from the same score.

## 5.1 A take is shown as notation

Record `t21-loop4` (Session View, as in the Phase 2 tests).
- Expect: the Score panel shows 4 measures on a grand staff (treble above bass), the key signature, 4/4, the tempo mark
  (quarter note = 127), notes where *Score as text* has them, rests, and a measure rest in the left hand where there is none.
- The line next to the buttons says "4 measures, N pages · laid out in X ms".

## 5.2 The three-minute piano part

Record `t23-piano-3min` (As played).
- Expect: about 93 measures; the first lines appear at once; scrolling draws further pages. Triplets are bracketed with a 3,
  ties join notes over bar lines, chords have one stem, beams stay inside the beat.
- Time: "laid out in" stays well under 1 s (the plan's limit for 200 bars; 90 bars takes about 0.3 s).

## 5.3 Clicking

- Click a note: it turns red and the line under the score says e.g. "Right hand · measure 5 · voice 1 · eighth note Eb5".
- Click a note of a chord: the whole chord is selected ("quarter chord C4 E4 G4").
- Click a rest, a tied note ("tied to the next note"), a triplet note ("eighth triplet").
- Click the white of the page: the selection is cleared.

## 5.4 Views, zoom, resizing

- *Pages* shows printed-page proportions with a gap between pages; *Continuous* has no gaps. The choice is remembered.
- − / + change the size of the notation (50 % to 200 %); *100 %* returns. The lines are broken again each time.
- Make the plugin window wider or narrower (drag its corner): the lines are broken again for the new width.
- Change a setting (Grid, Triplets, Hands split, Key): the score is engraved again and keeps its scroll position.
- Select another version: its score shows from the top.
- While recording, the score of the selected version stays on screen.

## 5.5 Saving

Save the set, close and reopen Live: the Score panel shows the same score for the selected version.

## 5.6 Reference clips

When the reference clips arrive: record each and compare the engraved score with the notation you expect.
