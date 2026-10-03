# Phase 8b tests in Ableton Live 11: selecting several notes, and select-same

New in the **Edit** bar, first row: **Select**: *All*, *Same pitch*, *Any octave* (for drums: *Same drum*). On the page:
- **Ctrl+click** a note or chord: one more (or one less). **Shift+click**: every note between the last one clicked and this one, in time (the staff of the first).
- A **rectangle dragged over empty paper** selects the notes it touches (with Ctrl: added to the selection).
- **Ctrl+A**: every note and chord of the score; **Ctrl+Shift+A**: every note of the pitch of the selected note (a drum: the same drum); **Ctrl+Alt+A**: the same note in every octave. **Esc** or a click on empty paper: no selection.
- The sentence under the score says "N selected · the one clicked last: ...". Every selected note is red.
- Every button and key of the Edit bar works on all the selected notes, as **one** undo step. If one note cannot take the change (too high for the piano, not playable on the guitar, a pitch on a rest), nothing changes and the message says so.

## 8b.1 Piano (`t43-edit-pages`, As played, piano)
- Click a note, Ctrl+click two more: three red notes, "3 selected". Ctrl+click one of them again: two. Shift+click a note a few bars on: the notes from the primary one to it are red.
- A rectangle from empty paper above the first line down over the first two bars: the notes in it are red. Click on empty paper: nothing selected.
- Ctrl+A: every note red, "N notes selected (all of the score)". Press **Pitch ▲** (or the Up arrow): all go up a half step, one message "Pitch changed (N notes)", the selection stays. **Ctrl+Z**: all of them back in one step. Ctrl+Y: again.
- Click one C and press **Same pitch** (Ctrl+Shift+A): every note of that pitch is red (in both hands). **Any octave**: the C of every octave. Then select two notes far apart (Ctrl+click) and press *Same pitch*: only the notes in the bars between them.
- Select all the notes of one bar, press **Staccato**: all get it; press again: all lose it. Select three, one of which has *mf*: choose *mf* in the Dynamic list: all have it; choose it again: none.
- Select two notes and press **Slur**: one slur from the first to the last. **Delete**: all become rests, one undo gives them back.
- A refusal: select a very high note and a low one, press **8va ▲** until the high one would be above C8: "That is outside the range of the piano. Nothing was changed."

## 8b.2 Guitar (`t44-edit-guitar`, Guitar)
- Click a note in the **tab**, Ctrl+click two more in the tab: they are red in the tab. Press Pitch ▲: the notes and their frets change, the tab notes stay selected. Ctrl+Z: all back.
- **Same pitch** from a tab note: the notes of that pitch are red in the tab. Select a note in the notation staff: the same, red in the notation staff.
- Select a low E and another note, press Pitch ▼: "That note cannot be played on a guitar ... Nothing was changed."

## 8b.3 Drums (`t31-drum-groove`, Drums)
- Click a hi-hat, press **Same drum**: every closed hi-hat is red. Choose *Crash cymbal 1* in the Drums list and press **Add drum**: a crash on every one of them (one undo step).
- Select every snare (Same drum) and press **Ghost note**: all in brackets; again: all without.
- The pitch keys (Up arrow) on a selection: "That is for pitched music."

## 8b.4 Saving
- Selections are not saved. After an edit of several notes: Ctrl+S, close Live, open it again: the edit is there; one more Ctrl+Z is not possible (the history is not saved).
