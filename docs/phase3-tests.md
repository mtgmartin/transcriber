# Phase 3 tests in Ableton Live 11

Claude runs these itself (see `docs/BUILD_PLAN.md` §8) with build 0.3.0 or later installed. The unit tests already
cover the logic (undo/redo, serialization, versions, the state codec, damaged state); these tests check that Live
stores and restores the state, and that the page works. Clips are in `test Project\Transcriber tests\`.

Setup: the `test Project` set, the Transcriber on track 7, 127 BPM. The plugin window shows the **Capture** panel on the
left and the **Versions** list on the right.

## 3.1 Takes become versions

1. Record three different takes: `t21-loop2`, `t21-loop4` and `t21-loop8` in Session View (as in Phase 2 test 2.4), each
   stopped with the plugin's Stop button.
2. Expect: three versions, *Take 1*, *Take 2*, *Take 3*; the newest is highlighted; each shows its time, note count and
   reading (One loop · 2 / 4 / 8 bars).
3. Press Record and Stop without playing anything: no new version; the page says nothing was recorded.

## 3.2 Version actions

1. Click *Take 1*: it becomes active; the piano roll and the reading show that take.
2. Rename *Take 2* to something with a non-ASCII letter (e.g. "Strofa č"): Enter commits, Esc cancels.
3. Duplicate *Take 3*: "Take 3 copy" appears after it and is active.
4. Delete the copy: the first click turns the button into `Delete "…"?` with Cancel; Cancel keeps it; confirming removes
   it and the previous version becomes active.
5. Change the reading of *Take 1* (One loop ↔ As played, loop length); another take is unaffected.

## 3.3 Save, close, reopen (the gate)

1. With three versions (one renamed, one with a changed reading), save the set (Ctrl+S).
2. Close Live and open the set again. Open the Transcriber window.
3. Expect: all three versions are back with their names, readings and active selection; the piano roll shows the same
   notes; *Compare with MIDI file…* with the original `.mid` still passes for each take (the raw capture is unchanged).
4. Record a fourth take: it is named *Take 4*.

## 3.4 Saved size

- The page shows the size saved with the set (a few KB for the takes above).
- A very long take (the 3-minute drums, `t23-drums-3min`) stays well below the 5 MB warning.

## 3.5 Old state

- Opening a set saved by 0.2.0 or earlier (the `test Project` set as it was before this test) gives no versions and no error
  (the page may say that an older test state was ignored).

## 3.6 Stability

- Open and close the window, and switch tracks, while versions exist: nothing is lost or frozen.
