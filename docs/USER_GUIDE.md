# Transcriber: user guide

Transcriber is a VST3 plugin for Ableton Live 11 (Windows). It records the MIDI that plays on its track and turns it into sheet music that you can edit and save as a PDF:
piano (two staves), drums, guitar and bass (with tablature). Everything it records is kept in your Live set, so the score is still there when you open the project again.

## 1. Install and open

1. Close Live. Open PowerShell **as administrator** in the project folder and run `powershell -ExecutionPolicy Bypass -File scripts\install.ps1`. It downloads the latest successful build from GitHub Actions (the GitHub command line tool `gh` must be installed and logged in) and copies `Transcriber.vst3` to `C:\Program Files\Common Files\VST3`. Wait until the build of the commit you want is green before you run it.
2. Start Live and let it scan plugins. Put **Transcriber** on a MIDI track as an instrument (its window is called "Transcriber/<track name>"). Click the wrench of the device to open the window; it can be made taller by dragging its corner.
3. It needs the Microsoft Edge WebView2 runtime (part of Windows 11 and of current Windows 10).

## 2. Record a take

- In the **Capture** panel press **Record**. It waits for Live's transport (it can already be running) and records every note that plays through the track, from clips, from a keyboard or from the arrangement.
  The recording ends when Live stops or when you press **Stop** (the same button).
- Every recording with notes becomes a **take** (version) of its own; an empty one makes none. Takes are listed under **Versions**: click one to show it, **Rename**, **Duplicate**, **Delete**. The score of a take never changes when you record again.
- **Reading** says how a looping clip is read: *As played* writes every pass; *One loop* writes only the repeating part (**Loop length** in bars; **Detect again** looks for it again). The plugin detects loops by itself and tells you what it found.
- The first note decides where the music starts: a first bar that starts late becomes a pickup bar (switch **Pickup bar** off to keep a full bar).

## 3. Notation settings (the Notation row)

| Setting | What it does |
|---|---|
| **Instrument** | Piano, Drums, Guitar or Bass. Can be chosen before the first recording; it is used for the next one. |
| **Grid** | The smallest note value kept, 1/4 to 1/128. 1/16 is the default; 1/32, 1/64 and 1/128 keep very fast notes (the report says if the grid is too fine for what was played). |
| **Triplets** | A beat may be written as a triplet when that fits the playing better. |
| **Transpose** | For a synth that plays other pitches than the notes it receives (an octave or key shift inside the synth): this many semitones (-48 to 48) is added to every note before the score is made, so you see what you hear. The recording itself is untouched. The next recording gets the same value. Not for drums. |
| **Tuning** (guitar, bass) | *Automatic* writes the standard tuning, and another one only when the notes need it (Drop D, a 7-string guitar, a 5-string bass, everything tuned down); or choose a tuning from the list, or **My own...** and type the notes of the strings from the lowest (`D2 A2 D3 G3 B3 E4`, `C#2 Db3`, or MIDI numbers). A tuning other than the standard one is written under the tab. |
| **Hands split at** (piano) | Notes from this pitch up go to the right hand; the program keeps a hand where it was playing nearby. |
| **Key** | *Detect*, or a key you choose. |

An edited score keeps its settings (they are greyed out): choose **Discard my edits** to write the score again from the recording with other settings. The only exception is the tuning of a guitar or bass:
it can be changed on an edited score, as an edit you can undo.

## 4. Look at the score

- **Pages** shows A4 sheets, exactly the sheets of the PDF; **Continuous** is one long page. **−**, **+** and **100%** change the size on the screen.
- **Title** and **Composer** are written above the score (and in the PDF). **Notation size** (small, medium, large) and **Margins** (10, 15, 20 mm) set the pages and the PDF.
- **Export PDF…** asks where to save a PDF of the pages as you see them (embedded fonts, black on white). Click a note or rest to see what it is under the score.

## 5. Edit the score

Click a note, chord or rest, then use the **Edit** bar or the keys. Every edit can be undone (**Ctrl+Z**, **Ctrl+Y**); **Discard my edits** goes back to the transcription.

**Selecting.** Click a note; click a chord, then one of its notes to pick just that one. **Ctrl+click** adds or removes a note, **Shift+click** selects everything in between, a **rectangle** dragged over the page selects the notes it touches,
**Ctrl+A** or **All** selects every note, **Same pitch** (Ctrl+Shift+A) every note of that pitch, **Any octave** (Ctrl+Alt+A) the same note in every octave (for drums **Same drum**). With several notes selected only their measures are searched. **Esc** clears.
Every button works on **all** the selected notes as one undo step; if one of them cannot take the change (too high for the piano, not playable on the guitar) nothing is changed and the message says why.

**Notes and rhythm.** Pitch ▲▼ (arrow keys; Ctrl+arrow for an octave), the letters A-G enter a note on a rest, **Length** 1 to 1/128 (keys 7 to 1; a longer value takes the rests and notes after it), **Dot**, **Tie**, **Rest** (Delete),
**Add a note** (a chord with an interval above), **Spelling** (J: C♯ or D♭), **Key** (the whole score is spelled again), **Voice** 1/2 (piano), **Stem** and **Beam** (Break, Join, Auto).
**Grace note**: *With slash*, *No slash*, *Normal note*: the note becomes a small note in front of the note after it (it takes no time: its time becomes a rest); not shown in the tab.

**Marks and text.** Dynamics, articulations and fermata (pressing a mark again takes it away; with several notes it is set on all, or taken from all), **Slur** and **Cresc./Dim.** from the selected note over 1 to 8 notes (or from the first to the last selected note),
**Text…**, **Tempo…**, **Clear marks**.

**Layout.** **New line here**, **New page here**, **No break**, **Measures per line**, **Spacing**.

**Guitar and bass.** You edit the standard staff; the tab follows (notes keep their strings when they can). Click a tab note and **Tab string ▲▼** moves it to another string. A note that cannot be played on the strings is refused.

**Drums.** Choose a drum in the list, then **Add drum** (at the selected hit or rest, in the voice chosen next to it, or the one of the drum map), **Change to this drum**, **Add as grace note** (a flam), **Ghost note** (in brackets).
**Move to voice 1 2 3 4** moves the hit. There is no limit on how many drums sound together or how many voices you use.

## 6. The drum map

The **Drum map** row says which MIDI note is which drum, where it sits on the staff, its notehead, its voice (1-4) and the velocity below which it is a ghost note. **Edit…**, **Import…**, **Export…** (maps are JSON files), **Delete**.
Notes that are not in the map are listed in the report and left out until you add them.

## 7. Saving

Everything (the recordings, the scores, your edits, titles, settings) is saved with the Live set (Ctrl+S in Live). The size is shown under the Versions; a very long project warns at 5 MB. If a saved state cannot be read (damaged, or written by a newer version) it is kept and written back unchanged until you record something new.

## 8. Keys

| Key | Does |
|---|---|
| Up / Down arrow, Ctrl+Up / Ctrl+Down | pitch a half step, an octave |
| A to G | note letter on a rest, or change the letter |
| 1 to 7 | length 1/64, 1/32, 1/16, 1/8, 1/4, 1/2, 1 |
| . | dot |
| T | tie |
| J | the pitch written the other way (C♯ / D♭) |
| X | turn the stem |
| Delete / Backspace | make a rest, take a note out of its chord, take a grace note away |
| Ctrl+Z, Ctrl+Y | undo, redo |
| Ctrl+A, Ctrl+Shift+A, Ctrl+Alt+A | select all, same pitch (drum), same note in every octave |
| Esc | no selection |

## 9. Good to know

- The score shows what was **played**: very short notes before a beat are real short notes (make one a grace note by hand). The report under the score lists what the program had to decide: notes far from the grid, notes it merged, notes it left out and why.
- Live does not call a plugin while its transport is stopped: recording starts when you press play (or when the transport already runs).
- The PDF is A4 portrait. Page numbers appear from page 2.
- Not included: playback of the score, capo, microtonal tunings, importing music, instruments other than the four.
