# Phase 2 results (Ableton Live 11.3.13, Windows 11, 2 Oct 2026)

Build 0.2.0 (CI run 36927640929), run by Claude in Live with `tools/live/` scripts (mouse and keyboard).
Live ran at 127 BPM, 48 kHz, 256-sample blocks. The test clips are in `tests/fixtures/` (`t21`–`t24`).
"Compare" is the plugin's *Compare with MIDI file…*, which matches the score notes (after the chosen reading)
against the exported clip by pitch and onset within 0.01 quarter notes.

**Go / no-go: passed.** Every item in the Phase 2 "Done when" list was run in Live and passed.
Two small things came out of the tests; both are fixed in the next build (see the end).

| Test | Result | Evidence |
|---|---|---|
| 2.1 Record, play, stop | **Pass** | Armed → Recording → Stopped in Arrangement and Session. 12 notes for a 4-bar clip, read as *As played*. The plugin's own Stop works while the transport keeps running. |
| 2.1 Cancel | **Pass, but the page showed nothing** | Pressing Record while Live is stopped: the window kept saying *Stopped* until Live played. Live does not run the plugin while the transport is stopped, so the command waits. Fixed: the page now shows the pending command (*Armed*). |
| 2.2 Three-minute piano (Arrangement) | **Pass** | 801 of 801 notes matched, 0 missing, 0 extra, max onset error 0.00004 quarter notes (about 1 sample), reading *As played*. |
| 2.2 Three-minute drums (Arrangement) | **Pass**, with a finding | 1251 of 1251 notes matched, same error. The clip repeats exactly every 24 bars (my test pattern, by accident), so the first reading was *One loop · 24 bars* and 931 notes were left out. With *As played* all 1251 matched. See "Finding 1". |
| 2.3 Arrangement loop | **Pass** | Loop brace on 4 bars, 3½ passes: *One loop · 4 bars (from the host's loop)*, 4 passes, 12 notes, 12/12 matched. |
| 2.4 Session loop length | **Pass** | 1, 2, 4 and 8 bars, each launched in Session and stopped partway with the plugin's Stop: *One loop* with the right number of bars, *detected from the notes*, 3 / 6 / 12 / 24 notes, all matched the clip (max onset error 0.00004). |
| 2.5 Verse ×2, chorus ×4 | **Pass** | *As played*, 24 bars, 73 notes (72 plus the first note of the next pass), the panel says "closest repeat every 4 bars, 20% differ". |
| 2.6 Edited clip, stopped partway | **Pass** | Both ways: switching to the edited clip at the pass boundary, and the real thing (select all in the clip, arrow up, while it loops). *As played* first (33–40% of the bars differ); *One loop* then gives new notes up to the stop point (coloured as the newest pass) and old notes after it. |
| 2.7 Meter change | **Pass** | Markers 4/4, 3/4 at bar 3, 4/4 at beat 16. The preview's bar lines are at 0, 4, 8, 11, 14, 16, 20: Live's real bar starts, including the short bar of 2 beats where the marker fell inside a 3/4 bar. Notes stay on their bar lines. |
| 2.8 Held note at stop | **Pass** | A 2-bar clip whose last note runs to the clip's end, looped and stopped with Live's transport while the note sounded: the note has its full length (duration difference 0.00004) in the compare. |
| 2.9 Diagnostics off by default | **Pass** | No log file was created during all of the tests; ticking *Write a diagnostic log* created one (`Transcriber 0.2.0`), and unticking stopped it. |
| 2.10 Stability | **Pass** | Switching to another track (window hides) and back while recording: no gap, same notes as an uninterrupted run (32 vs 38 notes in different time spans, same shape), no freeze. A plain `Stop` from the window while Live keeps playing works. |

## Findings

1. **A long clip that repeats inside itself reads as One loop.** The drum clip is 90 bars but its pattern repeats
   every 24 bars, so it is indistinguishable from a 24-bar clip played 3¾ times. The reading is shown at once and
   *As played* is one click away, and the page now says so. A recording of a Session loop and a long clip with
   internal repeats cannot be told apart from the notes alone; this stays a user decision.
2. **While the transport is stopped, Live does not call the plugin**, so the plugin cannot learn about a button press
   until play. Fixed by showing the pending command.
3. **Live asks "Import tempo and time signature data?" when a `.mid` is dropped into the Arrangement**; answer *No*
   (already in the plan). Dropping a clip on a Session slot that already holds a clip needs the old clip deleted
   first, or the drop is ignored.
4. Live hides the Transcriber window when another track is selected and shows it again when the track is selected
   again; recording continues in the background.

## Not tested

- Drum Rack routing with the 3-minute drums: the same clip was played into the Transcriber on a plain MIDI track
  (the capture only sees note numbers; the Drum Rack setup was verified in Phase 1).
- 44.1 kHz and drawn tempo envelopes (as in Phase 1).
- Freeze / Export Audio capture.

## Fixed in the next build

- The page shows *Armed* as soon as Record is pressed, even when Live is not running the plugin.
- *Compare with MIDI file…* moved into the Capture panel (it was inside the Phase 1 tools).
- The *One loop* text suggests *As played* for a clip that was played once.
