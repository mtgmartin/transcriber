# Phase 4 results (Ableton Live 11.3.13, Windows 11, 2 Oct 2026)

Build 0.4.0 (CI green), run by Claude in Live with the `tools/live/` scripts, Live at 127 BPM. There is no sheet-music
view yet, so the scores were read as text ("Score as text"). The reference clips from the user have not arrived yet,
so nothing here says yet whether the notation matches what a musician would write for real music.

**Gate: passed for what can be checked without reference clips.** Three findings came out of it (below); all three are fixed
in 0.4.1.

| Test | Result | Evidence |
|---|---|---|
| 4.1 A simple loop | **Pass** | `t21-loop1` (1 bar) and the 4-bar clip (the Live clip is `t21-loop4` with every note one semitone higher): key line, 1 / 4 measures, one voice, no warnings. The text is what the notes say: `r/4. C4/8 r/8. C5/16 r/4` in the right hand (a dotted-quarter rest, an eighth, a dotted-eighth rest, a sixteenth, a quarter rest) and `C3/4 r/4 r/2` in the left. Bars add up to 4 beats; accidentals appear only when the key signature does not already give them. |
| 4.2 Three-minute piano | **Pass** | Played from a Session clip (it loops after bar 90, so the take has 93 measures). 819 notes recorded: *Compare* says 801 of 801 matched, 0 missing, 18 extra (the restart of the loop), max onset error 0.00004. Up to 2 voices in a hand, no warnings. Bar 8 shows the triplet figures `< ( C5/8 D5/8 E5/8 ) >`; chords sit in the left hand with the bass. |
| 4.3 Grid | **Pass** | 1/16 → 1/8 changes the score at once (durations go to eighths). No "moved" note appears: the piano part's onsets are all on eighths already, only note lengths were 16ths. (The sheet says a line appears; it appears only for onsets far from the grid.) |
| 4.3 Triplets | **Pass** | Off: "44 notes were far from the grid and were moved to the nearest position." and the triplet groups become straight eighths. |
| 4.3 Hands split | **Pass** | 60 → 72: the label changes to C5 and the chords move to the left hand. |
| 4.3 Key | **Pass** | Eb major: "Eb major (3 flats), set by you", spelling follows (flats). |
| 4.3 Pickup bar, reading switch | **Not run in Live** | Covered by the unit tests (pickup, One loop / As played regeneration). The reading switch could not be tried after the restart because of finding 1. |
| 4.4 Save, close, reopen | **Pass** | Ctrl+S, Live quit and started again: 7 versions, the take's settings (Grid 1/8, Triplets off, split 72, Eb major, pickup on) and the warning line were back, the score text is the same as before (checked bars 16–18). State size 22.5 KB for 7 takes with 1207 and 819 notes. |
| 0.4.1 re-check | **Pass** | Installed 0.4.1: after reopening Live "As played" and "One loop" work on a saved take (Take 9: 10 measures as played, 4 as a loop); choosing G major sets key once ("G major (1 sharp), set by you"); with the key detected again the G of bar 3 is written `G4!` (natural) not F double sharp. |
| 4.5 Reference clips | **Waiting** | |

## Findings

1. **After reopening Live the reading buttons (One loop / As played / Detect again / loop length) were disabled** for
   saved takes: they were enabled only in the state "stopped", which exists only right after a recording. Fixed:
   enabled whenever a take is shown and nothing is being recorded.
2. **G was spelled F double sharp in C# minor.** PS13 voted equally for both spellings and the tie went to the first
   letter. A tie now goes to the spelling with fewer sharps or flats (G natural, shown with a natural sign). Unit test added.
3. **Choosing a key sent two settings**, so the score was made twice (the first time with the old major/minor flag).
   Now one setting (`key` = tonic × 2 + minor).

## Notes

- With a key set by hand the test piano shows spellings such as `Fb5`, `Cb3`, `Bbb5` in Eb major. That clip is random
  chromatic music (all twelve notes are used, nothing is tonal), so PS13 has nothing to follow. Whether real music comes
  out right is what the reference clips decide.
- Setting up the check: Live names MIDI 60 as C3, the plugin names it C4 (the usual convention). The clips in Session
  slots on tracks 7 and 8 all show the name "Track 0"; the piano clip was dropped into track 7 slot 9.
