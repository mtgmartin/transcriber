# Phase 2 tests in Ableton Live 11

Claude runs these itself (mouse and keyboard, see `docs/BUILD_PLAN.md` §8) in the `test Project` Live set,
with build 0.2.0 or later installed. The unit tests (`tests/core`, run by CI) already cover the same
cases with a simulated host; these tests check that Live behaves the way the simulation assumes.

Test clips are in `test Project\Transcriber tests\` (made by `tools\live\make-phase2-fixtures.ps1`):

| File | Content |
|---|---|
| `t21-loop1/2/4/8.mid` | 1, 2, 4 and 8 bars; every bar is different |
| `t22-verse.mid`, `t22-chorus.mid` | 4 bars each, different notes |
| `t22-loop4-edited.mid` | `t21-loop4` with every note a semitone higher (stands in for an edited clip) |
| `t23-piano-3min.mid`, `t23-drums-3min.mid` | 90 bars at 120 BPM, with triplets and ghost notes |

Setup: tempo 120, 4/4. The plugin window shows the **Capture** panel; **Record** arms it. The
**Test tools from Phase 1** section holds *Compare with MIDI file…* (compares the score notes, after the
chosen reading, with a `.mid` file) and the **Write a diagnostic log** switch.

## 2.1 Record, play, stop (both views)

1. Arrangement: put `t21-loop4` on the track. Press Record (the pill says *Armed*), play from bar 1, stop after 4 bars.
   - Expect: pill *Recording* while playing, then *Stopped*; 12 notes; reading *As played*.
2. Session: launch the clip, press Record while it plays.
   - Expect: recording starts at once (the transport is already running); stop with the plugin's **Stop recording**
     button while the transport keeps running (*Stopped*, the capture is kept).
3. Press Record, then **Cancel** before playing: back to *Idle*. Press Clear after a recording: empty.

## 2.2 Three-minute parts, note for note

For piano and for drums (Instrument Rack with a Drum Rack and a Transcriber chain), in **Arrangement** and
in **Session** (clip launched once, stopped by Stop All Clips after it ends):

1. Record the whole clip once; reading must be *As played*.
2. *Compare with MIDI file…* with the same `.mid`.
   - Pass: missing 0, extra 0, max onset error below 0.01 quarter notes.

## 2.3 Arrangement loop

1. Put `t21-loop4` in the Arrangement, loop brace over it, loop on. Record, let it play 3½ passes, stop mid-pass.
2. Expect: reading *One loop · 4 bars (from the host's loop)*; the preview shows 12 notes; compare with
   `t21-loop4.mid` passes.
3. Press **As played**: the note count equals the raw count (all passes, in order).

## 2.4 Session loop length

For each of `t21-loop1/2/4/8` launched in Session and looped 3½ times (or 5 for the 1-bar clip): Record, stop mid-pass.

- Expect: *One loop* with the right number of bars (1, 2, 4, 8), source *detected from the notes*; compare with the
  clip's `.mid` passes.

## 2.5 Clip sequence

Session: launch `t22-verse`, let it play twice, then launch `t22-chorus` (during the verse's last bar), let it play 4 times, stop.

- Expect: reading *As played*; 24 bars; the note count equals the raw count. (The panel notes the closest repeat, every
  4 bars, and how many bars differ.)

## 2.6 Edited clip, stopped partway

Session: launch `t21-loop4`, record; after two passes select all notes in the clip and transpose them up a semitone
(while it plays); stop recording partway through the third pass.

- Expect: reading *As played* (the edit makes the passes differ). Press **One loop**: the loop is 4 bars, and the
  score has the new (higher) notes up to the stop point and the old ones after it. The preview colours show which
  pass each note comes from.

## 2.7 Meter change

Arrangement: a clip across 4/4 → 3/4 → 4/4 (markers at bars 1, 3 and 5, as in the Phase 1 project). Record a pass.

- Expect: the preview's bar lines follow the meter (bar numbers and widths); notes on bar lines stay on them.

## 2.8 Held note at stop

Session: a looping clip with a long note. Stop recording while the note sounds.

- Expect: with *One loop* the note takes its length from the previous pass; with only one pass it ends at the stop.

## 2.9 Diagnostics are off by default

- No log file is created until *Write a diagnostic log* is ticked. After ticking, `%APPDATA%\Transcriber\logs`
  gets a `phase1-…jsonl` file with the usual content.

## 2.10 pluginval and stability

- CI runs pluginval at strictness 5 on every build (already green).
- In Live: switch tracks and close/reopen the plugin window while recording; recording continues, no freeze.
