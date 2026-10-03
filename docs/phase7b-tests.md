# Phase 7b tests in Ableton Live 11: spelling and layout

New in the Edit bar (a second row): **Spelling** (the same pitch written the other way), **Key** (the whole score in another key),
**Voice** 1 / 2, **Stem** Auto / Up / Down, **Beam** Break / Join / Auto. Keys: **J** writes the pitch the other way, **X** turns the
stem (auto, up, down, auto). Still piano scores only.

- **Spelling:** C sharp becomes D flat and back; a white key such as E can be written F flat; D (which has only a double-sharp or
  double-flat other name) says so. Notes joined by a tie change together. A chord changes all its notes.
- **Key:** the list holds all 15 major and 15 minor keys. Choosing one writes the key signature and spells every note for it again (the
  pitches stay), then the accidentals are worked out again. It is one undo step. It does not need a selection.
- **Voice:** moves the selected note or chord to voice 1 or 2 of its measure. Where it was, a rest stays; the other voice gets the note
  (a new voice if there was none, written with stems down). If that place in the other voice already has a note, nothing happens.
  A voice above the first that is only rest is not written.
- **Stem / Beam:** a stem can point up or down instead of being left to the program. *Break* starts a new beam at the selected note, *Join*
  joins it to the note before it even across the edge of a beat, *Auto* gives the choice back to the program.

Test clip `t42-edit-layout` (made by `tools\live\make-phase7-fixtures.ps1`, in `test Project\Transcriber tests`): bar 1 eight eighth notes
C4 to C5, bar 2 F# G Bb B (quarters), bar 3 two C# half notes (spelled D flat), left hand C3 whole notes. Record it as piano. **It is detected
as F minor: choose C major in the Notation Key list before the first edit** (the settings are locked after an edit).

## 7b.1 Beams and stems
- Bar 1 shows four pairs of beamed eighths. Select the E4, press **Join**: C D E F are under one beam. **Undo**.
- Select the D4, press **Break**: C alone, D alone (no beam), E F joined. **Auto**: back to pairs.
- Select the C4, press **Down** (or key X): its stem points down; the line below says "stem down". **Auto** brings it back.

## 7b.2 Spelling
- Bar 2: select the F# (the first note) and press **Spelling** (or J): G flat, and the G after it gets a natural sign. Again: F sharp.
- Select the second note of bar 3 (D flat), press J: C sharp. Tie bar 3's two notes (T on the first), then J on either: both change.
- Select a D and press J: "That pitch has only one usual spelling."

## 7b.3 Key
- Choose **G major** in the Key list of the Edit bar: the key signature has one sharp, the F# of bar 2 loses its sharp sign, the Bb is written A sharp.
- Choose **E flat major**: three flats, Bb has no sign, the F# is written G flat.
- Choose **A minor** (no sharps or flats). Press Ctrl+Z three times: each step goes back, the Key list follows.
- The line "Key: ..." under the Capture panel follows the choice (no "detected").

## 7b.4 Voices
- Select the E4 of bar 1 (any note) and press **Voice 2**: voice 1 has a rest there, a second voice with that note appears (stem down).
  The line says "voice 2". Press **Voice 1** on it: the score is as before and voice 2 is gone.
- Move two neighbouring notes to voice 2, then try to move a note of voice 1 onto the place of a note in voice 2: nothing changes.
- Move the last note of bar 1 to voice 2 and save with Ctrl+S, quit and start Live again: the two voices are back.

## 7b.5 Locked and speed
- After an edit the reading and settings are locked as in 7a; *Discard my edits* brings the first transcription back.
- Each change shows within a second.
