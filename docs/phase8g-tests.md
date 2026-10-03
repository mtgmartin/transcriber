# Phase 8g tests in Ableton Live 11: drums without limits on limbs

Nothing in the drum score refuses, warns about or merges hits because of how many are played together. The drum map still says which voice a drum goes to by default (hands in voice 1, feet in voice 2),
but **any drum can be in any of four voices**: in the Drum map editor (the Voice column has four choices), and when editing: **Add drum in voice** and **Change to this drum** use the voice chosen in the list
next to them ("as the map says" is the default), and **Move to voice 1 2 3 4** moves the selected hit. Layers 1 and 3 have the stems up, 2 and 4 down. Clip: `t86-drum-kit`.

## 8g.1 Many drums at once (`t86-drum-kit`, Drums)
- Record it as drums. Bar 1 has kick, snare, closed hi-hat, low tom, high tom and crash on every beat: all six are in the score (the five hand drums in one chord of voice 1, the kick in voice 2), no
  warning. The line under the score says "hands and feet in two voices". Bar 2: ride on the eighths, snare, kick and hi-hat pedal. No hit is left out ("N notes of no length or played twice at once were merged" must not appear).

## 8g.2 Any drum in any voice
- Click the crash of the chord of bar 1 beat 1 (twice: the first click selects the chord, the second one note) and press **Move to voice 3**: the crash is in a third voice (a new line of notes with the stems up), the rest of the chord stays; Ctrl+Z puts it back. Move it to **2**, **4**: it goes where you say. Move it to the voice it is in: "It is already in voice N."
- Choose **in voice 3** next to the drum list, choose *Open hi-hat*, select the hit on beat 2 and press **Add drum**: the hi-hat is in voice 3. With *as the map says* it goes to voice 1 again.
- Select a snare hit, choose *Side stick* and *in voice 4*, press **Change to this drum**: the hit becomes a side stick and moves to voice 4 (a ghost note keeps its brackets).
- Select the kick (voice 2) and **Change to this drum** a snare without choosing a voice: it stays where it is (a snare in the feet voice): no message about hands and feet.
- Click a hi-hat, **Same drum** (all the closed hi-hats), press **Move to voice 3**: all of them move, one undo step.

## 8g.3 The drum map
- *Drum map: Edit...*: choose Voice 3 for the ride and Voice 4 for the hi-hat, Save; the take is written again: ride in the third voice, hi-hat in a fourth. The voices that are used are written as layers in order (a take with only voices 1, 3 and 4 shows three lines of notes).
- Export the map and import it again: the voices 3 and 4 are kept.

## 8g.4 Saving and the PDF
- Ctrl+S, restart Live: the voices of the hits are as you left them. Export PDF…: four voices on the staff are readable in the PDF (the stems of voices 3 and 4 follow 1 and 2).
