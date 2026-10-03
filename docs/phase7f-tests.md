# Phase 7f tests in Ableton Live 11: editing drum scores

Drum scores can be edited now. A hit is a note with a **drum** from the drum map in use (the map you chose in the Drum map row: General MIDI, GM 2 or yours).
The edit bar shows what fits the instrument: for drums the pitch, spelling, key, voice, stem and slur controls are hidden and a **Drums** row appears.

- **Drums** row: a list of the drums of the map in use, and three buttons:
  - **Add drum**: puts the chosen drum at the selected note or rest. The map says which voice it belongs to (hands: stems up, feet: stems down). If a hit of that voice is
    already there, the drum joins it as a chord; on a rest the new hit is as long as the first note value that fits before the end of the beat.
  - **Change to this drum**: the selected hit becomes the chosen drum (a chord: click it once, then click one of its notes). A drum of the other voice is refused ("delete this hit and add the drum there").
  - **Ghost note**: the selected hit in brackets, or back to a normal hit.
- Everything else of the editor works as before: **Length** and **Dot**, **Rest** (takes the hit out), **Tie**, **Beam**, Dynamic, Articulation (accents are written for drums now), Text, Tempo, Layout.
- Not for drums (the message says so): Pitch, Add a note, spelling, key, voice, stem, slur and hairpin.

Use the clip `t31-drum-groove` (from Phase 6, in `test Project\Transcriber tests`; 4 bars: hi-hat eighths, kick on 1 and 3, snare on 2 and 4, an open hi-hat in bar 2,
a ghost snare in bar 3, a fill in bar 4). Record it, choose **Instrument: Drums** and the General MIDI map.

## 7f.1 Add
- Click the rest on beat 2 of the lower voice in bar 1 (the quarter rest between the two kicks). Choose **Hi-hat pedal** in the Drums list, **Add drum**: a pedal hi-hat in the space
  below the staff, on beat 2, stem down. The message says "Drum added."
- Click the second hi-hat of the first beat (the lower beamed note), choose **Ride cymbal 1**, **Add drum**: the two notes now form a chord on the hi-hat line and the ride line.
- Choose **Bass drum 1** and **Add drum** on a kick: "That drum is there already."
- Without a drum chosen ("choose a drum"): "Choose a drum in the list first."

## 7f.2 Change
- Click the snare chord of beat 2 in bar 1 once, then click the snare note in it. Choose **Side stick**, **Change to this drum**: the notehead changes to the side stick's place;
  the hi-hat of the chord stays.
- Click the kick, choose **Snare**, **Change to this drum**: refused (a hands drum in the feet voice): "That drum is played with the hands ...".

## 7f.3 Ghost note
- Click the ghost snare of bar 3 (in brackets), **Ghost note**: the brackets go. **Ghost note** again: they are back.
- Click a normal snare, **Ghost note**: it is in brackets.

## 7f.4 Length, rests, marks
- Click a hi-hat, **Rest**: a rest in its place (the beam is made again). Ctrl+Z.
- Click a kick, length **1/8**: an eighth note and a rest.
- Click the snare of bar 4 beat 2, Articulation **Accent**: a ">" above the snare.
- Dynamic **mf** on a note: "mf" under the staff. **Tempo…** and **Text…** work.
- Press the Pitch keys (Up/Down arrow) on a hit: "That is for pitched music ...".

## 7f.5 Undo, saving, other instruments
- Ctrl+Z walks back through every step, Ctrl+Y does them again. Ctrl+S, close Live, start it: the edited drum score is as it was left. *Discard my edits*: the first transcription.
- Choose Piano for another take: the edit bar shows the pitch controls again (no Drums row); Guitar shows **Tab string** and no Voice buttons.
