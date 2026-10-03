# Transcriber build plan

Last updated 2026-10-03. **Current state: Phases 0-6 done and tested in Live (the tab still to be confirmed by the user's own riffs). Phase 7a (editing notes and rhythms, piano) passed in Live as 0.7.1, 7b (spelling, key, voices, stems, beams) passed as 0.8.0; the user's reference clips are still to come.**

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
**What was built (build 0.6.0, 102 core tests, ~74000 checks):**
- `source/core/Instruments.*`: `InstrumentType` (piano, drums, guitar, bass), `DrumEntry` {note, name, loc, head, voice, ghostBelow}, `DrumMap` (JSON, find/set/remove), built-in maps `gm` and `gm2`, `Profile` {type, drum map copy}, `openStrings()` (guitar E2 A2 D3 G3 B3 E4, bass E1 A1 D2 G2).
- `source/core/TranscribeInternal.h`: the Builder (measures, beams, tuplets) moved here, now with a `noteMaker`, plus the steps every pipeline shares (`prepareNotes`, `analyseKey`, `addTempoMarks`, `finishScore`). Notes that land in one slot twice now count as merged for every instrument.
- `Drums.cpp` `transcribeDrums`: one percussion staff; notes placed by the map (`loc` = MEI @loc), hands voice 1 (stems up), feet voice 2 (stems down); a hit is written as long as the time to the next hit of its voice but not past the end of its beat group (kick on 1 and 3 = quarters with rests); ghost notes by velocity in brackets; open hi-hat with an "o" above; same drum twice in a slot written once; notes not in the map are listed in a warning.
- `Fretted.cpp` `assignTab` (dynamic programming over the chords: candidates on distinct strings within a 4-fret span, cost = span, height, open-string bonus, hand movement, string changes) and `transcribeFretted` (one voice, chords; a chord lasts until the next one or as long as its longest note; notes out of range or too many at once are warned about and left out; standard staff with 8vb clef + tab staff with the same events). `transcribe(capture, settings, profile)` picks the pipeline.
- `Mei.cpp`: percussion staff (`clef.shape="perc"`, `@loc`, `head.shape="x"`/diamond, `head.mod="paren"` for ghosts, `<dir>o</dir>`, `stem.dir` by voice), tab staff (`notationtype="tab.guitar"`, `<tuning>` courses, `<tabGrp>` with `<tabDurSym/>`), 8vb clefs, bracket for tab. Verified in Verovio 6.3 (glyph.name and head.shape="circle"/"plus" are NOT supported on notes; that is why the open hi-hat uses a text mark).
- `Document`: `defaultProfile` (new takes get it), `drumMaps` (the user's maps), `Version::setProfile`; the profile (with the map copy) is saved with every version; scores are made again at load as before.
- `CaptureService`: `setInstrument`, `selectDrumMap`, `saveDrumMap` (a changed built-in map is saved as `user-N`), `importDrumMap`, `deleteDrumMap`, `getDrumMapJson`; the status carries `instrument`, `drumMaps`, `drumMapId`, `drumMapRevision`. The processor remembers the last note-on (`lastNote`, `noteCount`) for Learn; the page shows it.
- Page: Instrument select in the Notation row (hands split only for piano, key not for drums), `web/drums.js` (map select, editor table with place/notehead/voice/ghost, Learn, Add, Save, Import/Export through file dialogs, Delete with confirmation, warning for unmapped notes). Live shows MIDI 60 as C3: the editor shows both names.
- Tests: `tests/core/test_instruments.cpp` (maps, JSON, profiles, drum goldens incl. random takes, tab validity over 300 random sequences, tab = notation for 80 random takes, fixtures, MEI) and a document test; JUCE test `service: instruments and drum maps`. Fixtures `t31-*` from `tools/live/make-phase6-fixtures.ps1`.
- Limits: guitar/bass is one voice (a sustained note under a melody is cut at the next onset); standard tuning only, 22 frets, no capo, no bends/slides/hammer-ons; drum hits never longer than their beat; no drum-specific beaming rules beyond the beat groups; the drum positions of toms and Latin percussion are a first guess (editable).

**Original build list:**
**Build:**
- Drum profile and map editor with learn mode, GM/GM2 presets, JSON import/export, and a ghost-note threshold.
- Guitar and bass profiles, plus the tab algorithm.

**Done when:**
- A Drum Rack groove with a custom map gives a correct two-voice score.
- A guitar riff and a bassline give standard notation plus playable tab, confirmed by the user.

### Phase 7: editor
**Part 7a (build 0.7.0, 118 core tests, ~83900 checks): notes and rhythms of a piano score.**
- `source/core/Edit.h/.cpp`: `performEdit(Score&, UndoManager&, Json request)` with the operations `pitch` (semitones; +-12 keeps the spelling; spelled for the key: the notes of the key signature as it writes them, other notes with sharps in a sharp key and flats in a flat key), `letter` (a rest becomes a note in the octave nearest the music before; on a note it changes the letter), `duration` (a longer value takes the rests and then the notes after it; a shorter one leaves a rest), `dot`, `delete` (note or chord to rest; one note taken out of a chord), `interval` (adds a note above the top note), `tie`, `undo`, `redo`. `editBlocker(score)` says why a score cannot be edited (guitar, bass and drums: their second staff mirrors the first, to come later).
- How an edit works: it copies the measures of the staff and changes the copy (a voice is rewritten from "slots": gaps become rests, neighbouring rests join and are written in the usual values by `splitLength`, a bar of rest becomes a measure rest, beams are made again). Then `normaliseTies` (a tie stays only where the next event holds the same pitch) and `refreshAccidentals` (now a free function in `TranscribeInternal.h`, the Builder uses it too) run over the staff, and the measures that differ go in with one `ReplaceChildrenCommand` (new in `Commands.h`: swaps the children of several nodes; undo swaps them back). Events that stay keep their ids, so the selection survives. Triplet notes can change pitch or become rests, but not change length.
- `UndoManager` now holds a pointer (`rebind`) because the list of versions can move scores in memory. `CaptureService` keeps one history per version (not saved), `editScore(request)` (marks `scoreEdited`), `discardEdits()` (`Version::discardEdits` writes the score again), and the status carries `edit` {canUndo, canRedo, undoName, redoName, blocked}. While `scoreEdited` the reading, settings and instrument setters do nothing (the page greys them and offers "Discard my edits").
- Page: `web/edit.js` (edit bar, keys, discard), `web/score.js` (`select()`, a second click on a note of a chord selects just that note, `transcriberScore.selected()`); `describeNode` says "this note: E4" for a picked chord note.
- Tests: `tests/core/test_edit.cpp` (every operation applied and undone with an exact comparison; the steps of `docs/phase7a-tests.md` on the practice clip; 40 random takes with 60 random edits each, also in 3/4 and with pickup bars: the score stays well formed, every state undoes and redoes exactly, an edit and its opposite leave the rest of the score untouched) and the JUCE test `service: editing the score`. Fixture `t41-edit-practice` from `tools/live/make-phase7-fixtures.ps1`.
- Not in 7a: guitar/bass/drum editing, cut/copy/paste, moving notes in time, changing the length of triplet notes, inserting or deleting measures, spelling and voices (7b, below), markings (7c), page layout (7d).

**Part 7b (build 0.8.0, 123 core tests): spelling and layout of a piano score.**
- `Edit.cpp` operations: `respell` (the next spelling with at most one accidental, e.g. C# / Db; tied notes follow, a chord changes all its notes), `key` (fifths -7..7 and minor: every note is spelled again for the key with the same rules as `pitch`, accidentals redone, root `keyFifths`/`keyTonic`/`keyMode` set; one undo step made of three `SetPropertyCommand`s and a `ReplaceChildrenCommand` in a group), `voice` (1-4: the event becomes a rest in its voice and is written in the other voice of the measure, new voices are inserted in order; refused if that voice has a note there; triplets refused), `stem` (up, down, auto, flip: property `stem`, written as `stem.dir` in MEI and named in the click sentence), `beam` (break, join, auto: properties `beamBreak` / `beamJoin`, honoured by `rebeam`).
- `finish` now drops a voice above the first that is only a measure rest. `CaptureService` copies the key of the score back into the version after every edit (the page shows it).
- Page: second row of the Edit bar (Spelling, Key list of 30 keys, Voice, Stem, Beam), keys J (respell) and X (turn the stem).
- Tests: `test_edit.cpp` (respell incl. ties and chords, key change with exact undo, stems, beams, voices, the clip `t42-edit-layout` as in `docs/phase7b-tests.md`; the random edit test now also draws the new operations and checks voice order and empty voices), JUCE test `service: editing the score` (key).
- Not in 7b: beams and stems for guitar/drums, moving a note to a voice in another measure, cross-staff notes, courtesy accidentals.

**Part 7c (build 0.9.0, 126 core tests): markings and text of a piano score.**
- Data: marks on a note or chord are properties (`dyn` = ppp..fff, sfz, fp; `artic` = stacc, acc, ten, marc; `fermata`; `text` and `textPlace`); slurs and hairpins are `spanner` nodes under the score with `kind` (slur, cresc, dim), `from` and `to` (event ids); tempo marks are the existing `tempo` nodes of the first staff.
- `Edit.cpp` operations: `dynamic`, `artic` (toggles), `text`, `clear`, `slur`/`hairpin` (to the count-th following note or chord of the voice, rests skipped; same again takes it away; a crescendo replaces a diminuendo; group of `InsertNode`/`RemoveNode` commands), `tempo` (bpm 20-400 and/or text at the onset of the selected event, replaces the mark there, both empty removes it).
- `Mei.cpp`: `<dynam>`, `<fermata>`, `<dir>` for text, `<slur>`, `<hairpin>` in the measure where they start (spanners whose notes are gone or became rests are skipped), `@artic`; the click sentence lists the marks.
- Page: third row of the Edit bar and a small in-page dialog for text and tempo (`#dialog` in `web/index.html`, `openDialog` in `web/edit.js`).
- Tests: `test_edit.cpp` (marks, spanners incl. removal and replacement, tempo, MEI contents and well-formedness; the random edit test draws the new operations too). Checked in Verovio (browser harness): dynam, artic, slur, hairpin, fermata, dir and tempo all render.
- Not in 7c: guitar/bass/drum markings, title and composer (they go on the page and the PDF in Phase 8).

**Part 7d (build 0.10.0, fix 0.10.1; 129 core tests): page layout of a piano score.**
- Data: a measure of the first staff can carry `break` = "system" or "page" (written as `<sb/>` / `<pb/>` before it in the MEI); the score has at most one `layout` node with `systemSpacing` and `staffSpacing` (Verovio units, default 14 and 10). `CaptureService::getMei` sends the spacing next to the MEI (`layout`).
- `Edit.cpp` operations: `break` (id, mode system / page / none; same mode again removes it; the first measure refused), `perLine` (count 0-32: a line break before every count-th measure, other line breaks removed, page breaks kept; one undo step), `spacing` (system and/or staff, 4-40; creates the layout node).
- Page: fourth row of the Edit bar; `score.js` passes the spacing to Verovio and uses `breaks: "smart"` with `breaksSmartSb: 0` (the breaks of the score are kept and a line that does not fit is broken again; 0.10.0 used "encoded", which squeezed everything between two breaks into one line, and Verovio's default "smart" threshold of 0.66 ignored breaks of short lines) and says so when a drawn line does not start at a break of the score.
- Tests: `test_edit.cpp` (breaks, measures per line, spacing; the random edit test draws them too). Fixture `t43-edit-pages` (24 bars). Test sheet `docs/phase7d-tests.md`.
- Not in 7d: page size and margins, staff size (they belong to the PDF export, Phase 8), breaks in guitar/bass/drum scores.

**Part 7e (build 0.11.0, 135 core tests with the fixtures): guitar and bass scores.**
- Idea: the standard staff of a guitar or bass score is edited with the same operations as piano; the tab staff is rebuilt from it after every edit (`syncTab` in `Edit.cpp`, called from `finish`). A click in the tab is turned into the matching note of the standard staff (`notationIdOf`: same measure, voice, event and chord position), and the answer selects the tab note again (`tabIdOf`).
- Tab ids: every tab node has the id of its notation node plus `-t` (`deriveTabIds` in `Fretted.cpp` from the transcription on, 0.12.1; so ids stay stable across edits and undo, and unique; `Score` demands unique ids score-wide).
- Strings and frets: `syncTab` reads where every note was played before (old tab staff, matched by position to the old notation staff), keeps a note on its string if its pitch did not change, and places new or changed notes with `placeChord` (new in `Fretted.cpp`/`Notation.h`: the chord candidates of `assignTab` limited to the notes that stay, cost = own cost + 0.6 x distance of the hand from the last chord). A chord that cannot be played at all (outside the strings or 22 frets, no distinct strings) makes the whole edit refused ("That note cannot be played on a guitar ..."). Marks and spelling stay on the notation staff (the tab events carry no `stem`, `dyn`, `artic`, `text`, `step`, `accid` ...).
- New operation `string` (id of a tab note, dir up/down): the same pitch on the next higher/lower string (own undo step, tab only; refused for a chord as a whole, an occupied string, or a fret outside 0-22). Such a manual choice survives other edits. Page: "Tab string" buttons in the second edit row.
- `changeKey` skips tab staves; `voice` is refused ("A guitar or bass score has one voice."); `referencePitch` knows the G8/F8 clefs; `editBlocker` now only refuses drum scores.
- Tests: `test_edit.cpp` (apply/undo of pitch, interval, delete, duration, string moves, chords, marks, key, MEI; `tabProblems` checks that the tab always mirrors the notation: same events, same pitches, playable distinct strings, no notation properties; 30 random guitar/bass takes x 50 random edits from either staff; the clips `t44-edit-guitar` and `t45-edit-bass` as in `docs/phase7e-tests.md`).
- Not in 7e: drums (7f), alternative tunings, capo, moving a whole chord to another position by hand, fingerings and techniques (bends, slides).

**Part 7f (build 0.12.0, 138 core tests with the fixtures): drum scores.**
- The page sends the entry of the drum map it wants ("drum": note, name, loc, head, voice), so the score code does not need the map. New operations in `Edit.cpp`: `drumAdd` (a drum at the onset of the selected note or rest, in the layer of its voice: hands 1, feet 2; a missing voice layer is made; a hit already there becomes a chord, otherwise the rest is split and the hit takes the first note value that fits before the end of the beat group), `drumSet` (the selected note becomes another drum of the same voice; another voice is refused when the measure has both voices), `ghost` (toggles the `ghost` property of the selected notes).
- Not for drums (refused with a message): `pitch`, `letter`, `interval`, `respell`, `key`, `stem`, `voice`, `slur`, `hairpin`. No accidentals are worked out on a percussion staff. `editBlocker` now only refuses an empty score. Accents and other articulations are written for percussion notes in MEI (`@artic`).
- Page: a fifth row (Drums list + Add drum, Change to this drum, Ghost note), shown for drums only; `data-show` groups hide the controls the instrument does not have (pitch, spelling, key, voice, stem, slur for drums; voice for guitar and bass; Tab string only for guitar and bass). `web/drums.js` asks for the entries of the map in use (`drumMapNeed`) and tells `edit.js` through `window.transcriberDrums`.
- Limits: a kick-only part has its hits in layer 1 (stems up), so the lower voice of a mixed edit goes into a new layer 2; a drum cannot be added inside a triplet; the map itself is not edited here (Drum map row).
- Tests: `test_edit.cpp` (`testDrumEdits`, 25 random drum takes x 50 random edits, the clip `t31-drum-groove` as in `docs/phase7f-tests.md`). Test sheet `docs/phase7f-tests.md`.

Built in parts:

| Part | Scope |
|---|---|
| 7a | Notes & rhythms |
| 7b | Spelling & layout |
| 7c | Markings & text |
| 7d | Page layout |
| 7e | Guitar and bass |
| 7f | Drums |

- Each part ships as its own build.
- Every operation gets a unit test (apply, then undo, gives back the original) plus a check in Live.
- Keyboard shortcuts are allowed (Phase 1.4).
- Text is entered in plugin dialogs.

### Phase 8: PDF export (build 0.13.0, 139 core tests; Live check pending)
- **Title and composer:** two fields of the version (`Version::title`, `composer`; saved only when set; at most 120 characters, one line; `Document::setMeta`, counted in `Version::revision` so that the page asks for the MEI again). `CaptureService::setScoreMeta`, status `title`/`composer`, page event `setMeta`. The MEI has `<title>` and `<composer>` in the `titleStmt`; Verovio's `header: "auto"` then draws title (centred) and composer (right). A hand-written `<pgHead>` is NOT drawn by Verovio 6.3 (tried in `scoreDef`, in `score`, with `<rend>` nesting); the composer is only drawn from `<composer>` directly inside `<titleStmt>`. Without title and composer the page uses `header: "none"` (an empty title still reserves space).
- **One page geometry for the screen and the PDF** (`optionsFor` in `score.js`): A4 (210 x 297 mm), margins 10/15/20 mm, notation size small/medium/large = Verovio scale 70/85/100 (a staff is 7.2 mm at 100; one unit is 0.1 mm at scale 100, so mm x 1000/scale = units). The Pages view engraves with these options (`svgViewBox`) and shows the pages as wide as the window times the zoom; the PDF uses the same options plus `mmOutput`. Continuous view unchanged (with the title).
- **Text and fonts:** DejaVu Serif regular, bold, italic (npm `dejavu-fonts-ttf` 2.37.3, hash pinned in `cmake/Dependencies.cmake`, served as `font/ttf` from the web resources, `docs/THIRD_PARTY.md`). Verovio sets all text in `font-family="Times"`; `engraved()` swaps it to "DejaVu Serif" on the page and in the PDF (accents work; jsPDF's own fonts are ASCII only). svg2pdf.js takes the fonts registered with `addFont`.
- **`web/pdf.js`:** `exportPdf` engraves again with the PDF options, puts every page in a hidden `<div style="color:#000">` (svg2pdf reads `currentColor`, which would be the light colour of the dark page), calls `doc.svg` per page, then `send("savePdf", {base64, name})`; the plugin shows the native Save dialog (`savePdf` in `PluginEditor.cpp`, suggested name = title or take name; answers `pdfSaved` with `ok`/`error`/`cancelled`). `fixSymbols` rebuilds a tempo text that contains a Bravura character as one run with the quarter note U+2669 of DejaVu (svg2pdf placed the three pieces by the widths of another font and the note overlapped "Andante"). Other music symbols are paths and need nothing.
- Checked in the browser harness (`build/vero`, real jsPDF 4.2.1, svg2pdf 2.8.1, Chrome's PDF viewer): piano with dynamics, slurs, hairpin, fermata, tempo and text marks; guitar with tab; drums; a 90-measure piano take (5 pages in 2.2 s, 190 KB; a PDF with the font is about 100-140 KB). Not yet seen in Live, Edge or Acrobat.
- Limits: A4 portrait only; the last line of a short score is not stretched across the page (Verovio's default); no page numbers or footers; PDF metadata (title) not set because jsPDF writes only ASCII there.

### Phase 8b: selecting several notes (requested by the user 2026-10-03; built after the Phase 8 Live check, before Phase 9)
Two requests: (1) while editing, select more than one note, or all of them, and edit them at once; (2) select all notes on the same pitch (drums: the same instrument), a "horizontal" selection through the whole score.

- **Selection model** (`web/score.js`): today one id (`selectedId`). It becomes an ordered list with one *primary* note (the last one clicked; the Edit bar, the click sentence and the dialogs refer to it). Every selected note is drawn highlighted; the sentence under the score says "12 notes selected" (and "all C4" / "all Snare" when they share a pitch or drum).
  - Click: select just that note (as now). **Ctrl+click**: add or remove a note. **Shift+click**: every note between the last click and this one in time order, in the staff of the click. **Ctrl+A**: all notes and chords of the staff (the one the primary note is in; with nothing selected, the first staff). **Esc**: clear. A drag rectangle over empty paper selects the notes inside it (if it works well with the SVG; otherwise skipped and said so).
  - Tab staves (guitar and bass): a click in the tab selects the notation note as now (`notationIdOf`); every set of ids is translated the same way and mapped back for display (`tabIdOf`).
- **Select the same pitch / instrument** (the horizontal select): a button "Select same" in the Edit bar and a key (Ctrl+Shift+A; a double click on a note too, if free). Pitched notes: every note with the same sounding pitch (MIDI number, so C#4 and Db4 are the same); a second button/option "any octave" selects the same note name in every octave. Drums: every hit of the same drum (same drum-map note, so all hi-hat closed hits; open and closed are different entries). Guitar and bass: same sounding pitch in the notation (the tab follows). Scope: the whole score, or only the measures spanned by the current selection when it has more than one note (so "select a range, then all C4 in it" works). Chords: a chord note counts by its pitch, the chord itself when the selection holds one note of it.
- **Where the logic lives:** in the core, so it is unit-tested: new read-only requests `selectAll`, `selectRange`, `selectSame` in `Edit.cpp` that answer with the list of ids (`select`) and are not undo steps. The page sends them and shows the answer.
- **Editing the selection:** the requests of the Edit bar get `ids` (the list) next to `id` (the primary note). The core runs the operation on every note in time order (later first for operations that change lengths, so ids stay valid) inside one `ReplaceChildrenCommand`/group, so *one* undo step undoes the whole edit. If the operation is refused for any note (unplayable guitar note, tie impossible, pitch out of range) the whole edit is refused with a sentence that says which note and how many were affected; nothing is changed.
  - Applied to every selected note: `pitch`, `dot`, `duration`, `delete`, `tie`, `respell`, `stem`, `dynamic`, `artic`, `text`, `clear`, `ghost`, `drumSet`, `drumAdd` (the chosen drum is added on every selected note or rest), `interval`, `voice`. A lengthened note can eat a selected neighbour: that neighbour is then simply gone from the result.
  - `slur` and `hairpin` with several notes: one from the first to the last selected note of the voice. `tempo`, `break`, `key`, `letter`, `string`, `perLine`, `spacing` act on the primary note or the score only (the button says so when several are selected).
  - After the edit the answer's `select` is the list of the notes that are still there, so you can press Pitch ▲ again and again.
- **Tests:** (core) `selectAll/selectRange/selectSame` on piano (chords, ties, rests, triplets), guitar/bass (tab ids) and drums; the multi-note edit gives the same score as the same edits applied one note at a time; one undo/redo step restores/redoes the whole edit exactly; a refused note leaves the score untouched; the random edit tests also pick random selections. (JUCE) the new request through the service. (Live) `docs/phase8b-tests.md`: Ctrl+click, Shift+click, Ctrl+A, select same on piano / guitar / drums, transpose and delete a selection, undo, a refused edit, Discard.
- Limits to expect: selection is per version and not saved; no selection across the two staves of a piano at once if that proves unclear (decide when building); no copy/paste (not planned).

### Phase 8c: a grid finer than 32nd notes (requested by the user 2026-10-03; after 8b, before Phase 9)
The Grid list of the Notation row stops at 1/32. It gets **1/64** and **1/128** (for fast ornaments, trills, flams, and drum rolls the 1/32 grid squeezes together).

- What exists: one quarter is 960 ticks, so a 64th (60 ticks) and its triplet (40) are whole numbers; `Rhythm.cpp` already writes 64th values (`units` down to 60, MEI `sixty-fourth`). Missing, found by reading the code: `Quantize.cpp::straightSlot` clamps the grid to 4/8/16/32; the Grid list in `web/index.html` ends at 1/32; `Edit.cpp::validDuration` allows `dur` 1-64 only; `Rhythm.cpp` has no 128th (30 ticks, its triplet 20) in `units`/the tuplet table; `Mei.cpp` has no `dur` 128 name ("128th" in MEI); `TranscriptionSettings::grid` is documented as 4/8/16/32.
- Work: 1/64 = a slot of 60 ticks (triplet 40); 1/128 = 30 ticks (triplet 20). Extend the slot choice, the settings parse (old saved values still load; unknown values fall back to 16), the duration tables of `Rhythm.cpp` (lengths, ties, beam groups, tuplets with 64th/128th notes), `Mei.cpp` (`dur` 64/128 with the right beam count; Verovio draws them), the edit operations (duration shorter/longer through 1/128, `splitLength`, `groupEndAfter`, drum add) and the Grid list.
- Care: a finer grid keeps more of a human player's timing noise as tiny notes. The default stays 1/16. The report line says when a score has many very short notes with a grid this fine ("N notes shorter than a 32nd: is the grid too fine?"). Tuplets of 64ths only where a beat's triplet grid fits better, as now.
- Tests: core: quantise/transcribe clips with 64th and 128th material (straight and triplet) at each grid; round trip through MEI; the edit tests (apply/undo, random edits) with the new values; the existing tests unchanged at 4-32. Browser harness: Verovio draws 64th/128th beams without overlap. Live: `docs/phase8c-tests.md` (a fast-run clip recorded at 1/32 and 1/64, a drum roll, edit a 64th note).

### Phase 8d: grace notes (requested by the user 2026-10-03; after 8c, before Phase 9)
Today there are no grace notes: a note played slightly before another is quantised onto the grid (the same slot: a chord; otherwise a short real note that takes time from the main note). `grep` of the sources finds no grace support.

- **Data:** a note or chord of a voice can carry `grace` = "acc" (acciaccatura, small note with a slash) or "app" (appoggiatura, small note without a slash). A grace event has no duration in the bar: the measure's time sums skip it (`Score` checks, `normaliseTies`, `writeLayer`, `splitLength`/slots in `Edit.cpp`, beaming, the clip length of a measure). It belongs to the next non-grace note or chord of the voice (several grace notes in a row are allowed, up to 4); a grace note with no main note after it in its measure is refused (and a grace note whose main note is deleted or becomes a rest follows the next note or is removed, decided when building).
- **MEI:** `<note grace="unacc"|"acc" dur="8|16" ...>` before the main event (grace notes of a group in a `<beam>` as the engraver wants; stems up for a slash). Written in `Mei.cpp`; Verovio draws them small. The click sentence says "grace note".
- **Edit operation `grace`** (id or ids; mode acc / app / none): the selected note (or chord) becomes a grace note of the next note or chord of the voice; its length is given back to the main note (the main note and the rests around it take its time again, as for a delete + longer duration, in one undo step). The same mode again, or "none", turns it back into a normal note: it takes a 16th from the note before it (shortening it; refused if there is no room, e.g. the first note of a measure after a rest it would use). Works with several selected notes (Phase 8b). Refused for notes in triplets and for the last event of a measure.
- **Guitar and bass:** the grace note is written on the standard staff and the tab shows it too, small (`syncTab` copies the `grace` property; `tabProblems` is updated; the string/fret of the grace note is chosen like any other note). **Drums:** a grace note on a drum is the flam (a small slashed note of the same drum before the main hit); `drumAdd` can add one.
- **Detection (optional switch "Grace notes" in the Notation row, off by default):** a note that is very short (up to a 32nd) and lies within a 16th before a longer note of the same hand/voice is made a grace note of it instead of occupying grid time; the report says how many were made ("3 grace notes"). With the finer grid of 8c the same notes would stay as short real notes, so the switch is useful with grids 1/16 and 1/32. Edited scores are not re-read, so the switch is part of the settings and locked like the others once the score has been edited.
- **Tests:** core: grace notes survive the whole chain (`finish`, MEI, tab sync); edits around them (pitch, delete the main note, change its length, undo/redo exactly); measure sums with and without grace notes; the random edit tests draw `grace`; the detection on synthetic clips (a grace note before a beat, a fast run that must stay a run, a drum flam). Browser harness: Verovio draws acc and app, on guitar with tab and on the percussion staff. Live: `docs/phase8d-tests.md`.
- Not planned: grace notes that play before the beat in the *playback*/MIDI (the plugin only shows notation), ornament signs (trill, mordent), grace notes across a bar line.

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
  - Keys go to the window that has the focus: click on the plugin page header (about 1800, 60) before sending Ctrl+Z etc., or they reach Live's own undo.
  - Graceful Live restart: `(Get-Process -Id <id>).CloseMainWindow()`, then `Start-Process` the exe with the .als path (Ableton Live 11 Suite.exe is in C:ProgramDataAbletonive 11 suiteprogram) and wait with a monitor until the main window title appears. dropdowns in the plugin page: click, press down/up once per entry (one `key` each, input.ps1 has no repeat count), then enter.
  - never end live with stop-process: the next start asks to recover (answer No); close its window instead. Phase 7 window: the plugin window opens at the top right (about x 1186-2286); scroll the page with the pointer over the Capture panel, not over the score. The key "." from input.ps1 arrives as Delete (use the Dot button). In Live's browser, search for a clip, expand the folder, then drag the file.
  - Space is awkward (the plugin window takes focus); click the transport buttons instead. The orange *Back to Arrangement* button (2215, 145) returns a track from Session clips to its Arrangement.
  - `$input` is a reserved PowerShell variable; don't use it in scripts.
  - Live auto-hides plugin windows of unselected tracks.
  - Phase 6 window (taller layout, Score panel on top): Record is at about (1292, 742) for piano/drums and (1292, 782) with the Notation row on two lines; `tools\live\record-slot.ps1` takes the position. The plugin window is resized only by dragging its exact corner (about 1-3 px outside the border). Live comes back small from the taskbar: maximise it (button at about (1440, 16)). A file dialog with a pre-filled name needs Ctrl+A before pasting a path.
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

- **Done:** Phase 8 code (title, composer, A4 pages, PDF export; 0.13.0): its Live check is next. Phases 0-6 with their Live tests (6: `docs/phase6-results.md`). Phase 7e (guitar and bass editing) and 7f (drum editing) passed in Live on 0.12.0 (`docs/phase7e-results.md`, `docs/phase7f-results.md`); 0.12.1 (138 core tests) fixes two small findings of 7e (a click on one note of a chord in the tab; selection lost by undo), not yet seen in Live. Phase 7d (page layout, build 0.10.0, 129 core tests) passed in Live except for lines between two breaks being squeezed (`docs/phase7d-results.md`); the fix is in 0.10.1 and was confirmed in Live. Phase 7a (0.7.1), 7b (0.8.0) and 7c (0.9.0; `docs/phase7c-results.md`) passed in Live; 0.9.1 has two small dialog fixes (`docs/phase7a-results.md`): editing notes and rhythms of piano scores.
- **Waiting on the user:** (1) install 0.13.0 (admin PowerShell, Live closed, `scripts\install.ps1`, only after I say CI is green) so that I can run `docs/phase8-tests.md` (it also shows the 0.12.1 fixes); (2) the reference clips: about 10 clips exported from Live (.mid) with the notation you expect; (3) your own guitar riffs and basslines to confirm the tab (Phase 6 gate).
- **Next:** the Phase 8 Live check (`docs/phase8-tests.md`), then Phase 8b (selecting several notes and "select same pitch / drum", two requests of the user on 2026-10-03; see Phase 8b above), then Phase 8c (a grid finer than 32nd notes, a request of 2026-10-03), then Phase 8d (grace notes, same day), then Phase 9 (hardening).
