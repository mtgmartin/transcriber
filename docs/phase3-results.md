# Phase 3 results (Ableton Live 11.3.13, Windows 11, 2 Oct 2026)

Build 0.3.0 (CI run 37040960032 and later), run by Claude in Live with the `tools/live/` scripts. Live ran at
127 BPM, 48 kHz. "Compare" is the plugin's *Compare with MIDI file…* against the original `t21-*.mid` clip.

**Gate: passed.** Three takes were recorded, the set was saved, Live was closed and started again, and all versions
were back with their names, readings and selection; the raw captures compared equal to the original clips.

| Test | Result | Evidence |
|---|---|---|
| 3.1 Takes become versions | **Pass** | Three recordings (2, 4 and 8 bars, Session View) gave three versions, each with its time, note count and reading (One loop · 2 / 4 / 8 bars). The newest is selected. |
| 3.1 Empty recording | **Not run in Live** | Live restarts Session clips that were playing when the transport starts again, so the "empty" recording contained notes. The case is covered by the JUCE test (`service: an empty recording makes no version`). |
| 3.2 Select, rename, duplicate, delete | **Pass** | Clicking a version shows its notes and reading. Rename (Enter) works, including a pasted non-ASCII name `Strofa č & "A"`. Duplicate adds "Take 3 copy" after the source and selects it. Delete shows `Delete "Take 3 copy"?` with Cancel; Cancel kept it, confirming removed it and the previous version became active. Deleting all versions shows the explanatory text again. |
| 3.2 Reading per version | **Pass** | Changing *Take 5* to As played did not change the other takes; it was still As played after restart. |
| 3.3 Save, close, reopen | **Pass** | Ctrl+S, Live quit, Live started with `test.als`: 3 versions, same names (including the non-ASCII one), *Take 5* still As played, the selected version restored. *Compare* gave PASSED for *Take 4* (6/6 notes, max onset error 0.00004) and for the 8-bar take (24/24) after the restart, so the stored raw capture is unchanged. |
| 3.3 New take after reopening | **Pass** | The next recording is named *Take 8* (numbers are not reused after deleting *Take 7*). |
| 3.4 Saved size | **Pass** | 2.0–2.1 KB for the takes above (the `.als` is 129 KB in all). The 5 MB warning is covered by the unit tests (a 100,000-note capture is 3.9 MB of JSON before compression). |
| 3.5 Old state | **Pass** | The set saved by 0.2.0 loaded with no versions and the message "The saved state was written by an older test build and was ignored." |
| 3.6 Stability | **Pass** | Window opened, closed and reopened, tracks switched, Live restarted: nothing lost or frozen. |

## Findings

1. **A time-signature marker left in the Arrangement changes the bars of Session clips too.** The 4/4 → 3/4 → 4/4
   markers from the Phase 2 meter test were still in the set, so the first three takes were read as *As played* (the
   host's bars no longer matched the clips' 4-beat bars). After deleting the markers the same recordings were read
   as One loop 2 / 4 / 8 bars. This is correct behaviour (the bar lines come from the host), but a loop that spans a
   meter change will not be found.
2. A small page fix: with no versions, the reading text said "As played ()" and the loop box kept a stale number.
   The text is now "Nothing recorded yet." (fixed in the next build).
3. Numbering: version names count recordings ("Take N"), they are not renumbered when one is deleted.

## Not tested in Live

- The empty recording (see above), a state over 5 MB, and a state written by a newer Transcriber (all covered by the
  unit tests: `tests/core/test_score.cpp`, `tests/juce/state_tests.cpp`).
