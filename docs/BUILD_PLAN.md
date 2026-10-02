# Transcriber build plan

Last updated 2026-10-02. **Current state: Phases 0-4 done and tested in Live. Phase 5 (sheet-music view) is built and green in CI (build 0.5.0); its Live check (docs/phase5-tests.md) and the user's reference clips are still to come.**

Transcriber is a Windows VST3 plugin for Ableton Live 11. It records the MIDI that plays on its track,
in Session or Arrangement View, and turns it into editable sheet music for piano, drum kit or
guitar/bass, with A4 PDF export. Editing the score never touches the MIDI.

The illustrated version of this plan (with the research sources) is at
https://claude.ai/artifact/DKgFpy21eNeM8hECT8LyFT. This file is the working copy. Update its
**Status** section at the end of every session.

---

## 1. How to work on this project

### Working rules (from the user)
- **Verify everything, don't assume, and ask when something is unclear.** Mark claims
  Verified / Partial / Unverified. Check library APIs against the pinned version's source,
  e.g. `gh api repos/juce-framework/JUCE/contents/<path>?ref=9.0.3`, before writing code.
- **Claude writes all the code.** The user directs and tests.
- **The plugin only builds on GitHub Actions**, but the JUCE-free core and its unit tests compile locally
  with MSYS2 g++ (see Build loop); run them before every push.
- **Claude runs the tests in Live itself, using mouse and keyboard only** (see §8).
  - Do **not** install Live Remote Scripts or control surfaces, and don't change Live
    preferences. The user declined this.
  - Modifying and saving the `test Project` Live set is allowed.
- Commit each step with a clear message and push. CI does the building.
  - End each commit message with the `Co-Authored-By` attribution line.
- Outward-facing or irreversible actions need the user's OK first (e.g. creating repos or
  releases, deleting things).

### Environment (verified)
- Windows 11 Pro, i7-11800H, 16 GB RAM. **C: has about 9 GB free, so there is no local C++ toolchain.**
  Everything compiles on GitHub Actions.
- Monitors:
  - Main monitor: 2560×1440 at 100% (Live runs here).
  - Second monitor: 1920×1080 at 125%, physical origin (321, 1440). The Claude app is here.
- Keyboard layout: **Croatian.** Typing `\` needs AltGr, so paste paths through the clipboard instead.
- Installed tools:
  - **MSYS2 g++ 14.2** at `C:msys64Crt64in` (found in Phase 2): only for the JUCE-free core and its tests.
  - Git 2.56.0 and GitHub CLI 2.102.0, logged in as `mtgmartin`.
  - Ableton Live 11 Suite 11.3.13.
  - WebView2 runtime 154.x.
- There is **no Python or Node.** Use PowerShell (5.1) or Git Bash.
- **The PATH in the app's PowerShell can be stale.** Prefix git/gh commands with:
  `$env:Path = [System.Environment]::GetEnvironmentVariable("Path","Machine") + ";" + [System.Environment]::GetEnvironmentVariable("Path","User");`
  In Git Bash, use `export PATH="$PATH:/c/Program Files/Git/cmd:/c/Program Files/GitHub CLI"`.
- **The shell is not elevated.** Installing a build into `C:\Program Files\Common Files\VST3` needs the
  user to run `powershell -ExecutionPolicy Bypass -File scripts\install.ps1` from an admin
  PowerShell, with Live closed.
- The session scratchpad gets cleared between days. Keep anything reusable in the repo (e.g. `tools/live/`).

### Repository
- Public repo: https://github.com/mtgmartin/transcriber
- Commit identity: `mtgmartin <75752557+mtgmartin@users.noreply.github.com>`. It is set per repo, and the no-reply address keeps the user's real email private.
- `test Project/` is the user's Live set, **gitignored**. Never commit it.

| Path | What it is |
|---|---|
| `CMakeLists.txt` | Plugin target. **Codes `Mtgm`/`Trsc` must never change**, or saved Live Sets lose the plugin. |
| `cmake/Dependencies.cmake` | Pinned, hash-checked downloads: WebView2 SDK nupkg 1.0.3485.44, and from npm Verovio 6.3.0, jsPDF 4.2.1, svg2pdf.js 2.8.1. It also appends the Verovio ready handler (see §6). |
| `.github/workflows/build.yml` | windows-2022 runner, VS 2022 generator, pluginval v1.0.4 at strictness 5, uploads the `Transcriber-VST3` artifact. |
| `scripts/install.ps1` | Downloads the latest successful build and copies it to the VST3 folder. Needs admin. |
| `source/core/` | The capture core, **no JUCE dependency** (namespace `trs`): `CaptureEngine` (audio thread: state machine and lock-free ring), `CaptureModel` (records to notes, segments, bars, tempo; holds the reading), `Reading` (loop detection and the One loop / As played rules). |
| `source/core/` (more) | `Notation.h` lists the pipeline: `Quantize.cpp` (clean-up, bars, pickup, quantising, tempo marks), `Rhythm.cpp` (lengths, ties, tuplets, beam groups), `Key.cpp`, `Spelling.cpp` (PS13), `Voices.cpp` (hands and voices), `Transcribe.cpp` (builds the Score, `dumpScore`). |
| `source/` | Plugin glue: `CaptureService` (drain thread, versions, saved state, UI data), `StateCodec` (header, checksum and gzip of the saved state), `MidiCompare` (the MIDI-file comparison), `DiagnosticLog` (JSONL log, **off unless switched on in the page**), processor, WebView2 editor. |
| `tests/core/` | Unit tests for the core: `test_main.cpp` (capture), `test_score.cpp` (JSON, score, undo, versions, documents), `test_transcribe.cpp` (the transcription pipeline), `MidiReader.h` (reads the fixture `.mid` files), `SimHost.h` (a simulated Live: blocks, loop-split blocks, bar lines, tempo lag, stop flush) and `TestSupport.h` (CHECK macros). Run by CI with ctest; run locally with g++ (see Build loop). |
| `tests/juce/` | `state_tests.cpp`: tests that need JUCE (the state codec and `CaptureService`, driven by the simulated Live). A second ctest target, built by CI only. |
| `web/` | Bundled UI page (`index.html`, `app.js`, `style.css`) and three MEI test scores. |
| `tests/fixtures/*.mid` | Test clips used in Phase 1 (format in §8). |
| `tools/live/` | PowerShell helpers to drive Live and read logs (§8), and `make-phase2-fixtures.ps1` (Phase 2 test clips). |
| `docs/phase1-tests.md`, `docs/phase1-results.md`, `docs/phase2-tests.md` | Test sheets and results. |

### Build loop
0. Run the core unit tests locally (about 10 s; Git Bash):
   `export PATH="$PATH:/c/msys64/ucrt64/bin"; g++ -std=c++17 -O1 -Wall -Wextra -Wshadow -Wconversion -Wsign-conversion -Isource -Itests/core source/core/*.cpp tests/core/test_main.cpp tests/core/test_score.cpp -o /tmp/t/core_tests.exe && /tmp/t/core_tests.exe` (after `mkdir -p /tmp/t`)
   (Check g++'s exit status itself: piping it to `head` hides a failed compile and runs a stale exe.)
1. Edit, commit, then `git push`.
2. Watch CI:
   - `gh run list --repo mtgmartin/transcriber --limit 1`
   - `gh run watch <id> --repo mtgmartin/transcriber --exit-status`
   - A build takes about 4.5 min (about 2 min of that is fetching JUCE).
3. Check the log for warnings in our code:
   - `gh run view <id> --log | grep -E 'warning C[0-9]+' | grep -i 'source[/\\]'`
   - Keep this at 0.
4. Have the user install the build (admin), then test in Live (§8).

### Gotchas found so far
- **pluginval.exe is a GUI-subsystem program.** PowerShell doesn't wait for it, so launch it with `Start-Process -Wait -PassThru` and check `.ExitCode`.
- **Don't name CMake variables `<Package>_ROOT`** (e.g. `WEBVIEW2_ROOT`). CMake treats them as find_package hints.
- **JUCE 9 moved `AudioProcessor`** into `juce_audio_processors_headless`. `#include <juce_audio_processors/juce_audio_processors.h>` still works.
- **JUCE 9 `Graphics::setFont`:** use the float overload, e.g. `g.setFont (16.0f)`.
- **Verovio:** set options **before** `loadData`.
- **Verovio drums:** position notes with `@loc` and **no** `@oct`: 0 = bottom line, 5 = 3rd space (snare), 9 = space above the staff (hi-hat), 10 = first ledger line above (crash), -1 = space below (hi-hat pedal). Noteheads use `head.shape="x"`.
- **Verovio tab:** `staffDef notationtype="tab.guitar" lines="6"`, `<clef shape="TAB"/>`, `<tuning tuning.standard="guitar.standard"/>`, and `tabGrp` holding `note tab.course tab.fret` (course 1 = top line).
- **svg2pdf.js drops `@font-face`.** Text uses jsPDF's standard fonts, which are ASCII only; add a TTF for non-ASCII titles.
- **Notes land on a sample, not exactly on the line.** At 127 bpm a downbeat can arrive 2e-5 quarter notes early. Anything that groups notes by bar or pass uses `boundaryTolerance` (0.01) in `Reading.cpp`; a test at 127 bpm caught this.
- **Editing files with scripts:** a perl `s|a|b|` whose text contains `||` silently matches nothing useful and corrupts the file (it did, twice). Use `s/\$old/$new/` in a script file, or the Edit tool; and don't mix the Edit tool with shell edits of the same file (Edit works from its own copy and silently undoes the shell edit).
- **`juce::var::operator[]` does not check the index** in release builds: an index past the end crashes (a CI test segfaulted). Look through `getArray()` instead.
- **MSVC reads `XXXX` even inside raw strings** (error C3850 for surrogates): write test text with a doubled backslash.
- **Windows long paths:** a plain `git clone` of JUCE fails on this machine ("Filename too long"), so JUCE can't be compiled locally.
- **PDF sizing:** `pageWidth:4200, pageHeight:5940, scale:50, mmOutput:true` gives exactly A4 with smaller notation.

---

## 2. Requirements (user decisions; do not re-ask)

| Area | Decision |
|---|---|
| Name | **Transcriber** |
| Platform | Windows only, VST3 only, Ableton Live 11 Suite 11.3.13 |
| Licence | Personal use only, not distributed (JUCE Starter/AGPL is fine). The repo is public. |
| Input | Record while Live plays the track. **One plugin instance = one track.** |
| Kind of MIDI | Mostly drawn in the piano roll, so it is already on the grid. Grid quantization is enough for v1. |
| Views | Session **and** Arrangement. In Session View the user sometimes loops one clip and sometimes launches several clips in a row. |
| One looping clip ("One loop") | Each pass overwrites the previous one. If stopped partway, the part covered by the final pass comes from it, and the rest comes from the pass before. A note belongs to the pass in which it starts. A note still held at stop takes the length of the same-pitch note at the same position in the previous pass; if there is none, it ends at the stop point. |
| Clips in sequence ("As played") | Everything written out in order, exactly as played, nothing overwritten. |
| Which reading | Detected automatically; the user can switch it and edit the loop length. Every pass is stored, so switching rebuilds the score from the capture. |
| Tempo & meter | Both can change within a song. **Tempo-mark rule (accepted):** show a mark where the tempo changes and then holds for at least one beat; mark ramps *accel.*/*rit.* followed by the new tempo. |
| Instruments v1 | Piano (grand staff); drum kit; guitar and bass (standard notation **plus tablature**). Standard tuning only: 6-string guitar, 4-string bass. |
| Drums | Ableton Drum Rack plus a custom map editor with learn mode, GM/GM2 presets, and JSON import/export. No specific third-party drum plugin. |
| Editing | All of: notes & rhythms · spelling & layout · markings & text · page layout. |
| Re-recording | Each recording becomes a new score version. Old versions and their edits are kept. |
| Paper | A4 by default (Letter available). |

---

## 3. Verified constraints that shape the design

- **A VST3 plugin cannot read clip data.** MIDI only arrives in `process()` during playback. ARA has no MIDI clips, and Live isn't an ARA host.
- **Live 11 won't load MIDI-only VST3s.** So Transcriber is declared as an instrument (`IS_SYNTH`, `NEEDS_MIDI_INPUT`), outputs silence, and has no MIDI output.
- **There is no key signature from the host.** Detect the key, and let the user override it.
- **Setups that work** (Phase 1.1):
  - A plain track.
  - An Instrument Rack chain next to the instrument or Drum Rack. Never inside a Drum Rack pad: it would get the pad's *Play* note.
  - *MIDI To* routing into `1-Transcriber`.
- **What Live 11 sends every block:**
  - Sent: PPQ position, BPM, time signature, last bar start PPQ, loop points, sample time, playing and looping flags.
  - Never sent: bar count, host time.
- **Arrangement loop:** the position jumps back, and the looping flag is true. Live splits the audio block exactly at the loop point, so positions stay exact.
- **Session View:** the position runs on continuously.
  - There is **no** loop signal and **no** clip-change signal.
  - The loop points shown are the Arrangement loop brace, not the clip's.
  - A new clip starts on the launch-quantization bar line.
- **Bar lines must come from the host's reported bar starts.**
  - Arrangement time-signature markers give a correct grid (e.g. 0, 4, 7, 10, 14).
  - Changing the global meter during playback re-grids from bar 1, creating an odd short bar.
  - The meter must be editable.
- **Tempo changes** are reported in the next block. Positions in quarter notes stay exact.
- **On stop**, Live sends CC 123 (All Notes Off) plus note-offs for held notes.
- **Keyboard:** the WebView page receives every key (letters, Space, Tab, arrows, F1–F12, Ctrl+C/V/Z), and Live reacts to none of them.
  - While the page has focus, Live's shortcuts don't work.
  - Show a hint to click outside the plugin to get them back.
- **Window:**
  - Resizing works.
  - The page is sharp at 125% scaling.
  - Live keeps hidden editors alive, so WebViews aren't recreated when switching tracks.
- **State:** 5 MB saves and restores intact (getState 8 ms, restore 17 ms). Keep the state under about 5 MB and warn above that.
- **Freeze** *does* feed MIDI and sets the VST3 offline flag (about 37× real time). Capture doesn't depend on it; it's a possible later feature.

---

## 4. Architecture

```
Live clip --playback--> [Instrument Rack chain | MIDI To] --> Capture engine (audio thread)
   --lock-free FIFO--> Raw capture (read-only, all passes kept)
   + Instrument profile / drum map --> Transcription pipeline --> Score document (editable, stable IDs)
   --> MEI writer --> Verovio JS in WebView2 --> Editor UI --(edit commands)--> Score document
                                             --> jsPDF + svg2pdf.js --> PDF (native Save dialog)
   Raw capture + Score --> plugin state (gzip, schemaVersion) saved in the .als
   Plugin audio output: silence only; no MIDI out.
```

**Why the MIDI can't change:**
1. No API exists for a plugin to write a clip.
2. The plugin has no MIDI output.
3. The raw capture is read-only; edits only touch the score document.

**Stack** (all verified):

| Part | Choice |
|---|---|
| Plugin framework | JUCE 9.0.3, via FetchContent (bundles VST3 SDK 3.8.0) |
| Build | CMake ≥ 3.22, MSVC (VS 2022) |
| UI | JUCE WebBrowserComponent on WebView2 (static loader) |
| Engraving | Verovio 6.3.0 JS (LGPL-3.0) |
| Music fonts | Bravura and Leland (OFL) |
| PDF | jsPDF 4.2.1 + svg2pdf.js 2.8.1 (MIT) |
| Validation | pluginval |
| Unit tests | JUCE `UnitTest` (to be added) |
| Fallbacks (not needed so far) | alphaTab (tab), native Verovio C++, libharu, WebView2 `PrintToPdf` |

### Data model
| Entity | Fields |
|---|---|
| `PluginState` | schemaVersion, activeVersionId, versions[], drumMaps[], uiPrefs. Gzip-compressed into VST3 state. |
| `Version` | id, name, createdAt, rawCapture, profileSnapshot, transcriptionSettings, score |
| `RawCapture` | notes[] {onPpq, offPpq, pitch, velocity, channel, passIndex}, passes[], reading {mode: oneLoop or asPlayed; loopStartPpq; loopLengthPpq; source: host, detected or user}, stopPpq, tempoMap[], meterMap[] (from host bar starts), sampleRate, hostInfo. Read-only except for `reading`. |
| `InstrumentProfile` | type, staves, clefs, transposition, strings (standard tuning), drumMapId. A copy is stored with each version. |
| `DrumMap` | entries[] {midiNote, name, staffPosition, notehead, voice, articulation, ghostVelocityThreshold} |
| `Score` | meta; parts → staves → measures → layers → events; spanners; layout. Stable IDs equal the MEI `xml:id`s. Changes only through `Command` objects (undo/redo). |

---

## 5. Transcription pipeline (v1)

| # | Stage | Method |
|---|---|---|
| 1 | Clean-up | Pair note-on/off and close notes at CC 123/stop. Drop zero-length notes and duplicates. Apply the reading: One loop uses the overwrite rule; As played lays out all passes in order. |
| 2 | Bars, meter, tempo | Bars from the host's reported bar starts plus time signature per block (meterMap). Tempo map. Tempo marks per the accepted rule. Marks can be edited. |
| 3 | Quantization | Grid with a user-chosen division (1/4 to 1/32). Per beat, pick the straight or triplet grid with the lower error (music21-style divisors 4 and 3). Quantize durations separately. Flag off-grid notes. |
| 4 | Staves & voices | Piano: split point (default middle C) plus a continuity heuristic, editable. Drums: voice from the drum map (hands up, feet down). Guitar/bass: one voice, chords allowed. |
| 5 | Durations | Split at barlines and at the 4/4 imaginary barline with ties. Rests fill gaps. Beam by meter, never across the imaginary barline (Berklee rule). |
| 6 | Key | Krumhansl–Schmuckler with Temperley's profiles, user override. |
| 7 | Spelling | PS13 (Meredith 2006). partitura (Apache-2.0) is a reference for testing. User can respell. |
| 8 | Tab | Dynamic-programming string/fret assignment that minimizes movement and span. Our own design; benchmark it on riffs. |
| 9 | Output | `Score` with stable IDs. |

**Session loop detection** (our design; Phase 2):
- Find the shortest bar count P, and a start bar, such that the recording repeats every P bars.
- If it repeats at least twice, the reading is One loop · P bars. Otherwise it is As played.
- Allow a small share of differing bars. Always show the result, and let the user change it.
- Known manual-fix case: a clip edited between passes reads as As played, and the user switches it to One loop.

**Drum defaults** (Weinberg / PAS):

| Instrument | Staff position | GM note |
|---|---|---|
| Kick | 1st space | 36 |
| Snare | 3rd space | 38 |
| Side stick | x on the 3rd space | 37 |
| Hi-hat (closed / open "o") | x in the space above the staff | 42 / 46 |
| Hi-hat pedal | x below the staff | 44 |
| Ride | x on the top line | 51 |
| Crash | x on the 1st ledger line above | 49 |
| Toms | Depends on the number of toms (Weinberg Ex. 4) | 50, 48, 47, 45, 43, 41 |

- Ghost notes go in parentheses below a velocity threshold.
- Unmapped notes get a warning; they are never silently dropped.

**Guitar and bass:**
- Guitar: treble clef with 8 below, sounding E2 A2 D3 G3 B3 E4.
- Bass: bass clef (8vb), E1 A1 D2 G2.
- The tab staff goes underneath the standard staff.

---

## 6. Phase status and remaining work

### Phase 0: setup and a plugin that loads (DONE)
Repo, CI, install script, and a silent VST3 instrument. Confirmed loading in Live by the user.

### Phase 1: feasibility tests (DONE, gate passed 2026-10-01)
Full results are in `docs/phase1-results.md`.

**0.1.1 fixes:**
- Verovio sometimes missed `onRuntimeInitialized` (1 load in 6). The build now appends the ready handler to the Verovio JS itself (`cmake/Dependencies.cmake`), and the page shows an error after 30 s.
- `devicePixelRatio` changes are now logged.

**Status:** 0.1.1 built and passed CI, but is **not yet installed** by the user.

**Not tested:**
- 44.1 kHz.
- Drawn tempo automation envelopes.
- Export Audio.

### Phase 2: capture engine (DONE, gate passed 2026-10-02)
**What was built (build 0.2.0, CI green, 27 unit tests):**
- `CaptureEngine`: idle → armed → recording → stopped. Arm waits for the transport to play (or starts at once if it already does). Recording ends when Live stops or the user presses Stop. Only note-on/off and CC 120/123 are recorded. Records cross to the other thread in a lock-free ring (32768 records); overflow is counted and flagged on the capture.
- `CaptureModel`: capture time = song time of the first block, then continuous. A jump of more than `0.05 + 2 blocks` quarter notes starts a new *segment*; it is a loop *wrap* if it goes backwards while the host's looping flag is on. Bar lines are the host's reported bar starts (a bar line before the latest one is ignored), the meter comes with each. The note-offs and CC 123 Live sends in the block where it stops are flagged, so the notes they end are known to be **held at stop**.
- `Reading`: detection and resolving. Host wrap → One loop with the observed length. Otherwise the shortest bar pattern that repeats through the whole recording from the first full bar: at least 2 periods, at most 10% of bar pairs differ, and the differences never fill a whole period (that is a new section). Bars are compared by pitch, onset (±0.02) and length (±0.1, except notes cut by the stop). The best candidate (at most 50% differing) is kept even when not chosen, so switching to One loop by hand starts from it. One loop is resolved exactly as in section 2, including the held-at-stop rule; the bar lines and tempo come from the latest pass recorded in full.
- `CaptureService` + page: Record/Stop, Clear, One loop / As played, loop length in bars, Detect again, and a piano-roll preview coloured by pass. The Phase 1 test tools are under a collapsed heading; the MIDI-file comparison now compares the *resolved* notes.
- Limits of the first version: loops are found on whole bars only (a 6-beat clip in 4/4 reads as 2 clips = 3 bars); the loop starts at the first full bar; a loop wrap in the middle of a bar loses that bar line; recordings with a user relocation are kept in order but have no bar lines across the jump.

**Original build list:**
- Record states: Idle → Armed (wait for playing) → Recording → Stopped.
- Event PPQ = host PPQ + sampleOffset / sampleRate × BPM / 60. Verified exact in Phase 1.
- Lock-free FIFO with no allocation or locks on the audio thread. Reuse the `DiagnosticLog` pattern.
- Note pairing. Handle CC 123 and note-offs at stop.
- Tempo map and meter map from the host's bar starts.
- Pass detection:
  - Arrangement: a backward jump with the looping flag true.
  - Session: repeating-pattern search.
- The overwrite rule (One loop) and the in-order layout (As played), re-run whenever the reading or loop length changes.
- UI control to switch the reading and edit the loop length.
- Unit tests (JUCE `UnitTest` console target in CI) using a simulated playhead. Cases:
  - fixed blocks
  - Arrangement loops with a split block
  - Session-style continuous time
  - a clip sequence
  - stop mid-pass
  - tempo ramps
  - meter changes
- Keep the MIDI-file comparison tool.
- Temporary UI: a note list or piano-roll preview.

**Result:** all of the following passed in Live, see `docs/phase2-results.md`.

**Done when:**
- 3-minute piano and drum parts match their clips note for note in both views.
- In Live, editing a clip between passes and stopping partway gives new notes up to the stop point and old notes after it.
- Session loop length is detected correctly for 1-, 2-, 4- and 8-bar clips.
- Verse ×2 then chorus ×4 is read as As played and written out in full.
- A 4/4 → 3/4 → 4/4 song keeps every bar line in place.
- pluginval passes at strictness 5, and the unit tests pass in CI.

### Phase 3: score model, versions, saving (DONE, gate passed 2026-10-02)
**What was built (build 0.3.0, CI green; 48 core tests + 8 JUCE tests):**
- `source/core/Json.*`: a small JSON value (no JUCE). Doubles are written in the shortest form that reads back identically, object members keep their order, and all-number arrays are kept packed (8 bytes per number) so long captures stay small in memory.
- `source/core/Score.*`: the editable score is a **generic tree of nodes** (`id`, `type`, scalar `props`, `children`) with stable ids such as `note-17` (they become the MEI `xml:id`s). Types and nesting: score → part → staff → measure → layer → note/rest/chord (chord → note); spanner and layout sit under the score. Property values are numbers, strings or booleans only. `validate()` checks unique ids, legal nesting and scalar properties; JSON load runs it and refuses a damaged score.
- `source/core/Commands.*`: the only way the score changes. `SetProperty`, `InsertNode`, `RemoveNode`, `MoveNode` and `Composite`, each with `apply`/`revert`; `UndoManager` with grouping (`beginGroup`/`endGroup` make one undo step), a step limit and redo. A test applies 3000 random edits and undoes and redoes them exactly. Phase 7's editing operations are built from these.
- `source/core/Document.*`: `Document` holds `Version`s (id, name, created time, profile, settings, raw capture, reading, detection, score), the active version id, `uiPrefs` and `drumMaps`. Operations: add (named "Take N"; the numbers and ids are never reused), select, rename, duplicate (copy goes after the source and becomes active), remove (the neighbour before takes over). The raw capture is never edited; only the reading and the score are. Saved as JSON with `schemaVersion` (now **1**) and `migrateDocument` (a list of steps, step *i* upgrades *i* → *i+1*). A document from a newer schema is refused as `tooNew`; damaged data as `invalid`.
- `source/StateCodec.*`: what the host stores: header `TRSC`, container format, 0 = stored / 1 = gzip, JSON size and an FNV-1a checksum, then the JSON or its gzip. Anything else (empty, or the Phase 1 `TRS1` test state) is ignored without error.
- `CaptureService`: each ended recording that holds notes becomes a version at once (an empty recording makes none, and the page says so). Reading edits and the MIDI-file comparison apply to the active version. **An unreadable saved state (damaged, or from a newer Transcriber) is kept byte for byte and written back until the user records something new**, with a message in the page. The state size is worked out at most once a second; the page warns at 5 MB.
- Page: a Versions list next to the Capture panel (click to select, Rename, Duplicate, Delete with an in-page "Delete 'name'?" confirmation), the saved size, and load messages. The Phase 1 state-test panel and the Clear button are gone.
- Not built in Phase 3: there is no UI for editing the score (Phase 7), and the undo history is not saved. (Phase 4 fills the score.)

**Original build list:**
**Build:**
- `Score` model with stable IDs, and commands with undo/redo.
- Versions: create on stop, list, rename, duplicate, delete (with an in-UI confirmation).
- Serialization: JSON/ValueTree + gzip, schemaVersion plus a migration hook, and a state-size display that warns at 5 MB.

**Done when:**
- Round-trip tests pass.
- In Live: record 3 takes, save, close, reopen; all versions are back and the raw capture is unchanged.

### Phase 4: transcription pipeline (piano first) (CODE DONE, LIVE CHECK PENDING)
**What was built (build 0.4.0, 72 core tests, ~5000 checks):** `transcribePiano(ResolvedCapture, TranscriptionSettings)` in `source/core/` (JUCE-free), one file per stage:
- `Quantize.cpp` — stage 1 `cleanNotes` (zero-length notes dropped, a note played twice at once merged), stage 2 `buildBars` (the host's bar lines in ticks of 960 per quarter; Live's short bars are kept and marked `irregular`; time before the first bar line becomes a short bar; an extra bar when the last note runs on) and `applyPickup` (a first bar that is **more than half empty** becomes a pickup bar starting at the beat of the first note; exactly half is an ordinary bar that starts with a rest), stage 3 `quantize` (per beat: straight or triplet grid, whichever has the lower summed error, the triplet needing to win by more than 2 ticks; a lone note a third of a beat in is a triplet; durations are quantised separately, on the grid of the beat they end in; a note that starts and ends inside one triplet beat is written in triplet values; notes more than a quarter slot away are flagged `offGrid`; the same pitch cannot sound twice at once), and `tempoMarks` (the accepted rule: a mark where the tempo changes and then holds for at least one beat, "accel."/"rit." before a ramp's new tempo, a tempo already on the page is not repeated, marks sit on a sixteenth).
- `Rhythm.cpp` — stage 5. `splitLength` cuts at bar lines (done by the caller) and at the "imaginary barline" (the middle of 4/4, each group of 6/8, 9/8, 12/8), then writes each piece as one written value if a single aligned value fits, otherwise cuts at the strongest beat inside it (so an eighth then a quarter, never a dotted value that starts off the beat). A note on the bar's first beat that is a single value may cross the middle (dotted half); a rest only if it fills the bar. Triplet beats are written as 3:2 tuplets of eighths, 16ths or 32nds (the size is chosen from the note boundaries in the beat); a group that is completely covered is an ordinary value. `beatGroups` gives the beam groups (per beat, per dotted quarter in compound meters), so beams never cross the middle of a 4/4 bar.
- `Key.cpp` — stage 6: Pearson correlation of the length-weighted pitch-class distribution with Temperley's Kostka-Payne profiles (the values music21 uses) for all 24 keys. `fifthsForKey` and the F sharp / G flat choice (the spelling with fewer accidentals outside the key wins).
- `Spelling.cpp` — stage 7: PS13 (Meredith 2006), following the structure of partitura's `ps13s1` (10 notes before, 40 after). **Improvement over plain PS13:** the algorithm needs one note whose letter is known; instead of the first note it starts from the **tonic of the detected (or chosen) key**, which fixes flat keys that begin on a flat note (without it an A flat major scale came out as G#, A#, B#, ..., F##). The score's accidentals follow the usual rule: shown when the spelling differs from the key signature or from the same line earlier in the measure (a natural after a flat); not repeated on a tied note.
- `Voices.cpp` — stage 4 (piano): a note goes to the right hand at or above the split point (middle C), except within 3 semitones below / 2 above it, where the hand that played nearby in the last two beats keeps it. Notes with the same start and length are one chord (low to high); each chord goes into the first voice that is free, the highest chord first.
- `Transcribe.cpp` — builds the `Score`: part "Piano", staves G and F, a measure per bar (`n`, `num`, `den`, `startTick`, `ticks`, `pickup`, `irregular`), a layer per voice (voice 1 always; further voices only where used), events with `onset`, `ticks`, `dur` (1 ... 64), `dots`, tuplet and beam ids, notes with `pitch`, `step`, `alter`, `oct`, `vel`, `tie` (`i`/`m`/`t`), `accid` (shown accidental) and `offGrid`; `tempo` nodes in the first staff's measures; key in the root (`keyFifths`, `keyTonic`, `keyMode`). `dumpScore` prints a readable text form (used by the golden tests and the page): `S1 v1: ( C4/8 D4/8~ ) D4/4 [C4 E4 G4]/2`, `R` = measure rest, `!` = shown accidental, `( )` beam, `< >` tuplet, `~` tie.
- `Version` now holds the settings, the score, the key and a report. **The score is made when a version is created and again when the reading or a setting changes, unless the user has edited it (`scoreEdited`).** An unedited score is not saved in the state (it is made again when the state is loaded: small states, and a better transcription later improves old takes); an edited one is saved.
- Page: Notation row (grid 1/4–1/32, triplets, hands split at, pickup bar, key), a line with the key, measures, voices and warnings, and "Score as text".
- Tests: every stage alone (tables of cases), 20 golden scores written from musical reasoning and then checked against the output, the fixture MIDI files read through `tests/core/MidiReader.h` (the 3-minute piano clip comes out as 90 measures, 801 notes, no warnings), and 150 random clips that must keep every voice filling its measure, every note its length and every tie its partner.
- Limits of this version: the finest grid is 1/32 (shorter notes are lost to the grid); triplets are 3:2 only (no quintuplets, no triplet quarters across two beats); no double-dotted values; a bar with an odd length (Live's short bar) is not split at all; one key for the whole piece; the hand split is per note, not per chord, so a chord that straddles the split can be divided; swing is not recognised (it is flagged as off the grid); `5/4`, `7/4` use plain beats. The 10 reference clips from the user are still to come: they are the real test of "100% correct".

**Original build list:**
**Build:**
- Stages 1–7 as separate functions with unit tests.
- Golden files (fixture MIDI → expected score JSON), including:
  - ties over barlines, triplets, chords, overlaps
  - a pickup bar
  - meter changes
  - tempo marks and ramps
- The user supplies about 10 reference clips.

**Done when:** grid-quantized clips come out 100% correct, and key and spelling are correct or the errors are listed and accepted.

### Phase 5: rendering and UI shell
**What was built (build 0.5.0, 80 core tests, ~19000 checks):**
- `source/core/Mei.h/.cpp`: `scoreToMei(Score, options)` writes MEI 5.1 (grand staff, key and meter, meter changes as section-level `scoreDef`, pickup as measure 0 with `metcon="false"`, ties as `@tie`, `<tuplet>`/`<beam>` nested by extent, chords, `mRest`, tempo marks with a quarter-note symbol). Every node id is the `xml:id`. `describeNode(Score, id)` gives the sentence shown when a note is clicked.
- `CaptureService::getMei()` (cached by version id + revision) and `describeScoreNode()`; the capture status carries `scoreKey`. Page events: `needMei` -> `mei {key, mei}`, `nodeInfo {id}` -> `nodeInfo {id, text}`.
- `web/score.js` + the *Score* panel at the top of the page: Verovio engraves the MEI; *Pages* (A4 proportions) and *Continuous* (no gaps) views, zoom 50-200 %, lines re-broken when the window is resized, click to select (red; a note in a chord selects the chord), view and zoom remembered in `localStorage`. Verovio lays out the whole score on load (about 0.45 s for 200 bars, measured in the browser pane); the pages are drawn only when they come near the visible area. The score of the shown version stays on screen while recording.
- Window default size 1100x860.
- Tests: `tests/core/test_mei.cpp` (well-formed XML, unique ids that all exist in the score, one element per note/rest, measure count, ties, tuplets, meter changes, pickup, escaping, click sentences, the fixture clips and 40 random clips). Looking at the engraving: `build/vero/` (scratch, gitignored) has a Verovio copy, a tiny web server (`serve.ps1`, started with `.claude/launch.json`) and `mkpage.sh`, which copies the real page with a stub for the plugin bridge.
- Not done yet from the original list: the toolbar's version picker and profile choice (the Versions panel and the Notation row already do this; profiles come with Phase 6).

**Original build list:**
**Build:**
- MEI 5 writer.
- Bundled UI with page and continuous views, zoom, and click-to-select through `xml:id`.
- Toolbar: Record, version picker, profile, grid, key override.

**Done when:** a recorded piano take shows correctly in Live, and 200 bars re-render in under 1 s.

### Phase 6: drums, guitar, bass
**Build:**
- Drum profile and map editor with learn mode, GM/GM2 presets, JSON import/export, and a ghost-note threshold.
- Guitar and bass profiles, plus the tab algorithm.

**Done when:**
- A Drum Rack groove with a custom map gives a correct two-voice score.
- A guitar riff and a bassline give standard notation plus playable tab, confirmed by the user.

### Phase 7: editor
Built in four parts:

| Part | Scope |
|---|---|
| 7a | Notes & rhythms |
| 7b | Spelling & layout |
| 7c | Markings & text |
| 7d | Page layout |

- Each part ships as its own build.
- Every operation gets a unit test (apply, then undo, gives back the original) plus a check in Live.
- Keyboard shortcuts are allowed (Phase 1.4).
- Text is entered in plugin dialogs.

### Phase 8: PDF export
- Proper margins and A4 notation size. Phase 1's PDF had almost no left margin and small notation.
- Embedded fonts for titles and text.

**Done when:** piano, drum and guitar/tab PDFs open in Edge and Acrobat and match the screen.

### Phase 9: hardening
- Raise pluginval to strictness 8.
- Stress tests: 10-minute songs, 5 instances, rapid play/stop, dense MIDI, schema migration.
- Crash safety.
- A user guide.
- Add the Steinberg validator via pluginval `--vst3validator` (it needs a built validator).

---

## 7. Risks still open
| Risk | Mitigation |
|---|---|
| Session reading misdetected (an edited clip reads as As played, or a repeating sequence reads as One loop) | Always show the reading, allow a one-click switch, and keep every pass. |
| Off-grid notes | Flag them; allow re-quantizing a selection. |
| Verovio tab or percussion edge cases | Feed MEI only. alphaTab is the fallback for tab. |
| Large state | Compression, a warning at 5 MB, an optional side file. |
| Slow iteration (CI only) | Unit tests and the simulated host catch most issues before Live testing. |

---

## 8. Testing in Live: how Claude does it

Tools in `tools/live/`. Copy them to the scratchpad or run them in place.

| Script | Use |
|---|---|
| `shot.ps1 -Out f.png -Scale 0.5 [-X -Y -W -H]` | DPI-aware screenshot of a screen region (physical pixels). Read the PNG to see it. |
| `input.ps1 "click x y; wait 300; key ctrl+s; type 120; drag x1 y1 x2 y2; slowdrag x1 y1 x2 y2 steps; scroll x y -5; rclick x y; dclick x y"` | SendInput mouse and keyboard. Synthetic keys arrive with an empty `KeyboardEvent.code`, but `key` is correct. |
| `midi.ps1` → `Write-MidiFile path notes lengthBeats` | Notes are `@(pitch, startBeat, durBeats, velocity)`. Live names MIDI 60 as C3. |
| `analyze-log.ps1 -Path <log.jsonl>` | Summarizes a plugin log: events, keys, transport, loop wraps, notes. |
| `phase2-run.ps1 -Seconds N -Shot f.png` | Arrangement: clear, arm, play N seconds, stop, screenshot of the plugin window. |
| `phase2-session.ps1 -SlotY y -Seconds N [-Launch2Y y2 -Launch2At s] [-TransportStop]` | Session: the same, launching the clip in the slot at screen y (clip sequences with a second launch). |
| `compare-file.ps1`, `check.ps1 -Path f.mid -Shot f.png` | Click *Compare with MIDI file…*, paste the path in the file dialog, screenshot. |
| `make-phase2-fixtures.ps1` | Writes the Phase 2 test clips (`t21`–`t24`) and copies them to the Live project. |

**Method:**
- **Test clips:** write `.mid` files into `test Project\Transcriber tests\`. They appear in Live's browser under *Current Project*, and you drag them into clip slots or the arrangement.
  - Live asks whether to import tempo/time signature when you drop into the Arrangement; answer **No**.
- **Ground truth:** the same `.mid` files, using the plugin's "Compare with MIDI file…" tool.
- **Logs:** each instance writes `%APPDATA%\Transcriber\logs\phase1-<date>-<time>-<instance>.jsonl`.
- **File dialogs:** paste paths via the clipboard. Save the user's clipboard first and restore it afterwards.
- **Finding plugin windows:** locate them with Win32 `EnumWindows`/`GetWindowRect` (title `Transcriber/<track>`) rather than guessing coordinates.
  - The title bar is about 15 px below the window's top edge.
- **Live behaviour to remember:**
  - Phase 2 coordinates (2560x1440 monitor): the plugin window is resized tall (drag its corner down) so the Capture panel is fixed: Record (1292, 206), Clear (1478, 206), One loop (1314, 248), As played (1404, 248), Detect again (1692, 248). Transport Play (1114, 60), Stop (1136, 60). In Session View, track 7's slots are at x = 983, y = 121 + 17.5 per slot. These move if the layout changes; take a screenshot first.
  - Live **does not call the plugin while the transport is stopped** (idle instrument), so a command from the page only reaches the audio thread at the next play.
  - Space is awkward (the plugin window takes focus); click the transport buttons instead. The orange *Back to Arrangement* button (2215, 145) returns a track from Session clips to its Arrangement.
  - `$input` is a reserved PowerShell variable; don't use it in scripts.
  - Live auto-hides plugin windows of unselected tracks.
  - Space stops the transport but **Session clips keep their playing state**. Use Stop All Clips, the master track's clip-stop button.
  - The global tempo field drag is extremely sensitive (it went to 999 BPM). Type values instead.
  - Insert time-signature markers via *Create → Insert Time Signature Change* (at the insert marker), then type e.g. `3/4`.
  - Freeze is in the track header's right-click menu.

**Current `test Project` contents:**
- Tracks:
  - 8-Transcriber: clips in slots 1–4 (loop4, clipA, clipB, timing) and the piano clip in slot 9.
  - DRUMS: Instrument Rack containing the Drum Rack plus a Transcriber chain.
  - 5-MIDI: MIDI To → 8-Transcriber.
  - 7-Transcriber.
- Arrangement: time-signature markers at bars 1 (4/4), 2 (3/4) and 4 (4/4).
- Saved with a 5 MB test state (6.4 MB file). Use "Use empty state" in the Transcriber window to shrink it.

---

## 9. Status (update every session)

- **Done:** Phases 0-4 with their Live tests (4: `docs/phase4-results.md`, build 0.4.1 verified in Live). Phase 5 code: build 0.5.0, CI green (80 core tests, JUCE tests, pluginval).
- **Waiting on the user:** (1) install 0.5.0 (admin PowerShell, Live closed, `scripts\install.ps1`) so the Score panel can be checked in Live; (2) the reference clips: about 10 clips exported from Live (.mid) with the notation you expect (a photo or description is enough), to turn into golden tests.
- **Next:** the Phase 5 Live check (`docs/phase5-tests.md`, then `docs/phase5-results.md`), then Phase 6: drums, guitar, bass and tab.
