# Phase 7f results: drums (Ableton Live 11.3.13, Windows 11, 3 Oct 2026)

Build 0.12.0 (CI green), run by Claude in Live with `t31-drum-groove` (6 measures as played, Instrument: Drums, the map "General MIDI (yours)"). No finding.

| Test | Result | Evidence |
|---|---|---|
| Edit bar for drums | **Pass** | Pitch, Spelling, Key, Voice, Stem, Slur and the other pitch controls are hidden; the **Drums** row (list, Add drum, Change to this drum, Ghost note) is shown. The list holds the drums of the map ("Bass drum 2 (35)", "Bass drum 1 (36)", "Side stick (37)", ...). |
| 7f.1 Add | **Pass** | The quarter rest in the lower voice of bar 1 + Hi-hat pedal (44) + Add drum: "Drum added.", a hit below the staff on beat 2 ("quarter hit hi-hat pedal"). Bass drum 1 on a kick: "That drum is there already." Change on a rest: "Select a hit first (or use Add drum on a rest)." Without a chosen drum: "Choose a drum in the list first." |
| 7f.2 Change | **Pass** | The snare of the chord on beat 2 (clicked twice: "this note: snare") + Side stick + Change: "Drum changed.", the note is an x in the snare place, the hi-hat of the chord stays. The kick + Snare + Change: "That drum is played with the hands: delete this hit and add the drum there." |
| 7f.3 Ghost | **Pass** | Ghost note: the hit in brackets; again: the brackets are gone. |
| 7f.4 Rest, length, marks | **Pass** | A hi-hat + Rest: an eighth rest ("Made a rest."). A kick + Accent: ">" below the stem; + length 1/8: an eighth note with the accent kept ("Length changed."). |
| 7f.4 Pitch keys | **Pass** | The Up arrow on a hit: "That is for pitched music. A drum score has the Drums row ...". |
| 7f.5 Saving | **Pass** | Ctrl+S, Live closed and started again: the pedal hi-hat, the eighth rest, the accented eighth kick and "edited by you" are there; *Discard my edits* (two clicks) brings back the first transcription. |

## Notes

- The recording of `t31-drum-groove` in Live is 6 measures (the clip loops in the Session slot); the bar numbers of the test sheet are those of the first 4.
- One note of the clip is not in the map (MIDI note 50, the tom of the fill): the warning is shown as before.
- Not tried in Live: Ride cymbal on a hi-hat (unit-tested), changing a chord note of the other voice, Dynamic and Text on a drum hit.
