# Phase 1 results (Ableton Live 11.3.13, Windows 11, 1 Oct 2026)

Tests were run in Live by Claude, driving the mouse and keyboard and reading the plugin's
JSON-lines logs. The test clips are in `tests/fixtures/`. Audio: 48 kHz, 256-sample blocks.

**Go / no-go gate: passed.** Tests 1.1, 1.2 (both views), 1.3 and 1.5 pass.

| Test | Result | Evidence |
|---|---|---|
| 1.1 MIDI arrives, original note numbers | **Pass** | See the setups below. |
| 1.2 Host info, loops, tempo, meter | **Pass**, with design consequences (below) | See the details below. |
| 1.3 Timing vs. the clip's notes | **Pass**: 61/61 notes matched, 0 missing, 0 extra | Max onset error 0.00004 quarter notes (about 1 sample). 44.1 kHz not run, because it needs a Live audio-preference change. |
| 1.4 Window and keyboard | **Pass**, with one finding | See the details below. |
| 1.5 Rendering in Live | **Pass** | All three test scores are correct in the plugin. One page load in six never started Verovio (fixed in 0.1.1). |
| 1.6 PDF from Live | **Pass** | Native Save dialog. 3 pages, A4 (595.28 × 841.89 pt), vector music, text kept as text. Margins and notation size need work in Phase 8. |
| 1.7 Saved state, 5 MB | **Pass** | Details below. |
| 1.8 Freeze (informational) | Live **does** feed MIDI and **does** set the VST3 offline flag | Details below. |

## 1.1 Setups

All of these delivered the original note numbers at exact positions:

- Plain track with Transcriber.
- Instrument Rack chain next to a Drum Rack (kick 36, snare 38, hi-hat 42).
- *MIDI To* routing from another track into the plugin (`1-Transcriber`).
- Instrument Rack with a piano: confirmed by you in Phase 0.

## 1.2 Details

**Every block provides:** position in quarter notes, tempo, time signature, last bar start,
loop points, sample position, and the playing and looping flags.

**Never provided:** bar count and host time.

**Arrangement loop:**
- The looping flag is on, and the position jumps from the loop end back to the loop start.
- Live splits the audio block at the loop point (e.g. 83 + 173 samples), so the note at the loop start
  arrives at exactly 0.0000.

**Session View clip loop:**
- No signal at all. The position runs on continuously, the looping flag stays off, and the
  loop points are unchanged (they stay at the Arrangement loop brace).
- **Loop length must be detected from the notes**, as the plan assumed.

**Session View clip change (A, then B):**
- No signal; only the notes change.
- The new clip starts on the launch-quantization boundary (bar line).

**Tempo:**
- Each change (a jump, and a fast drag) is reported in the next block.
- Positions stay exact, because they are in quarter notes.

**Meter, Arrangement markers (4/4 at bar 1, 3/4 at bar 2, 4/4 at bar 4):**
- Bar starts are reported correctly: 0, 4, 7, 10, 14.

**Meter, global field changed during Session playback:**
- Live re-grids from song position 0 under the new meter.
- This creates an odd short bar (548 → 549, then every 3 beats).
- **Bar lines must follow the host's reported bar starts, and the meter must be editable.**

**On stop:** Live sends CC 123 (All Notes Off) plus note-offs for held notes.

**Not tested:**
- Tempo automation ramps drawn as envelopes. The plugin only sees each block's tempo, which the fast drag already exercised.

## 1.4 Details

**Keyboard:**
- Every test key reached the page, Live reacted to none of them, and Ctrl+Z did not undo anything in Live:
  - letters and digits
  - Space, Tab, arrows, Delete, Backspace, Enter and Esc
  - F1–F12
  - Ctrl+C, Ctrl+V and Ctrl+Z
- The flip side: while the page has focus, Live's own shortcuts (e.g. Space for play) don't work
  until you click back into Live.

**Resize:** dragging the window edge resizes continuously and the page re-lays out.

**Scaling:** at 125% scaling (second monitor), the page renders sharp and 25% larger.

**Three instances:**
- Three instances ran with playback while switching tracks: no freeze, no JS errors.
- Live keeps hidden editors alive, so WebViews are not recreated when switching tracks.

## 1.7 Details

- `getState`: 5,242,896 bytes in 8 ms.
- Saving the set took about 0.7 s, and the set grew to 6.4 MB.
- After reopening: `Restored OK: 5 MB of test data matches` in 17 ms after the plugin
  started. Instances without test data restored too.

## 1.8 Details

- During Freeze, the plugin was prepared with `offline = true`, and 14,048 blocks carried the
  offline flag.
- 229 notes arrived.
- 75 s of audio rendered in about 2 s (≈37× real time).
- Capture still does not depend on this, but a "capture by freezing" option is possible later.

## Bugs found and fixed in 0.1.1

1. **Verovio sometimes never started** ("Loading Verovio…" forever, 1 page load in 6).
   - **Cause:** Verovio starts its WASM runtime asynchronously and only calls
     `onRuntimeInitialized` if a handler is already set. It has no "already started" flag, and a
     handler set by a later `<script>` can miss the call.
   - **Fix:** the build appends the handler to the Verovio file itself
     (`cmake/Dependencies.cmake`), so it always runs first.
   - The page now also reports an error if Verovio does not start within 30 s.
   - **Verification:** 8 consecutive page loads in a browser.
2. Moving the window to a monitor with different scaling was not logged (no resize event).
   The page now watches `devicePixelRatio` directly.

## Design consequences for Phase 2 and later

- **Capture:**
  - Compute positions from the host's quarter-note position plus the sample offset, as Phase 1 did (verified exact).
  - Handle CC 123 at stop.
- **Loops:**
  - Arrangement: detect a wrap from the backward jump plus the looping flag.
  - Session: detect from repeating notes (as planned). Clip changes are invisible, so the
    "One loop" vs. "As played" choice has to rest on the notes.
- **Bars and meter:**
  - Take bar lines from the host's reported bar starts, not from a fixed meter.
  - Let you correct meter changes.
- **Keyboard shortcuts in the editor are possible.** Show a hint that Live's shortcuts work again
  after clicking outside the plugin.
- **PDF (Phase 8):** add proper margins and a notation size suited to A4.
