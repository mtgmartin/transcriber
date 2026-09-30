# Phase 1 tests in Ableton Live 11

The Phase 1 build logs everything Live sends the plugin to a file in
`%APPDATA%\Transcriber\logs\` (one file per plugin instance). **Show log file** in the
plugin opens that folder. You don't need to read the logs: tell Claude when you've
finished, and it will read them.

Start each test with a **fresh Live Set** and note roughly what time you ran it, so its log is
easy to find. Tempo 120 BPM and 4/4 unless a step says otherwise.

## 1.1 MIDI arrives with the original note numbers

1. MIDI track with a piano (any instrument). Group it into an Instrument Rack (Ctrl+G),
   show the chain list, add a second chain and drop Transcriber on it.
2. Draw a 1-bar clip with 4 notes: C3, E3, G3, C4. Play it once.
3. Drum track: a Drum Rack with a kit. Group the Drum Rack into an Instrument Rack and add
   Transcriber on a second chain. Draw kick (C1), snare (D1), hi-hat (F#1) and play once.
4. Setup B: new MIDI track with only Transcriber. On the piano track, set *MIDI To* to that
   track and choose Transcriber in the lower menu. Play.

Write down: in each setup, did **Captured notes** go up?

## 1.2 What Live reports about position, loops, tempo and meter

In the **Host status** panel, and in the log:

1. **Arrangement, no loop:** play from bar 1 for 8 bars, stop.
2. **Arrangement, loop:** turn the loop brace on (bars 1–5), let it loop 3 times, stop in the
   middle of a pass.
3. **Session:** launch a 4-bar clip, let it loop 3 times, stop mid-pass. Then launch clip A
   (2 bars), and after it has played once launch clip B (2 bars, different notes). Stop.
4. **Tempo:** in Arrangement, automate the tempo: a jump from 120 to 90 at bar 3, and a ramp
   from 90 to 140 over bars 5–7. Play bars 1–9.
5. **Meter:** add a 3/4 time-signature marker at bar 3 and 4/4 at bar 4. Play bars 1–6.

Write down: anything shown as **not provided**, and whether **Looping** / **Loop points**
ever change in step 2.

## 1.3 Timing accuracy

1. Arrangement, 120 BPM. Draw an 8-bar clip that mixes chords, 16th notes, triplets and a
   few long notes.
2. Export it: select the clip → *File → Export MIDI Clip…* and save the `.mid` file.
3. In Transcriber: **Clear captured notes**, then play the clip **once** from its start, stop.
4. **Compare with MIDI file…** → choose the exported file.
5. Repeat at 48 kHz: *Preferences → Audio → Sample Rate* (if your interface allows it).

Write down: PASSED/FAILED, and the max onset error.

## 1.4 Plugin window

1. Resize the plugin window by dragging its corner. Does the page follow, and stay sharp?
2. Load **3** Transcriber instances on 3 tracks and open all three windows. Anything frozen
   or blank?
3. Click the keyboard box and press each key listed there. Write down which keys **do not**
   appear in the list, and whether Live reacted to any of them instead (e.g. Space started
   playback).
4. Optional: add `-_EnsureKeyMessagesForPlugins` as a line in
   `%APPDATA%\Ableton\Live 11.3.13\Preferences\Options.txt` (create the file if needed),
   restart Live, and repeat step 3.

## 1.5 Notation rendering

Switch between the three test scores. Compare with how they should look:

- **Piano:** grand staff with a brace, G major key signature, a dotted quarter, beamed
  eighths, a tie across the barline in the right hand, chords and a tie in the left hand.
- **Drums:** percussion clef. Bar 1: hi-hat crosses above the staff, snare in the 3rd space,
  kick in the 1st space, hands stems up, feet stems down. Bar 2: crash cross on a ledger line
  above the staff, toms, hi-hat pedal cross below the staff.
- **Guitar:** treble clef with a small 8 below, and a 6-line TAB staff with fret numbers
  (7-4-0, then 0, 0, then 0).

Write down anything that looks wrong.

## 1.6 PDF

**Export all three as PDF…** → save. Open the PDF in Edge (and Acrobat if you have it). Are
the three pages A4, sharp when zoomed in, and identical to the screen?

## 1.7 Saved state

1. Click **Use 5 MB test state**. Save the Live Set, close Live, reopen the Set.
2. Open Transcriber: **Last restore** should say `Restored OK: 5 MB of test data matches`.
3. Note whether saving or opening the Set felt slower than usual.

## 1.8 Freeze and export (informational)

With a Transcriber on a track that plays a clip: right-click the track → *Freeze Track*, then
*File → Export Audio/Video*. Nothing to write down; the log shows what arrived.
