# Transcriber

Windows VST3 plugin for Ableton Live 11 that turns the MIDI played on its track into editable
sheet music (piano, drums, guitar/bass + tab) with PDF export.

**Read `docs/BUILD_PLAN.md` first.** It holds:
- the user's decisions (don't re-ask them)
- verified facts about Live
- the environment quirks
- how to build via CI and how to run tests in Live
- the current status and next phase

Key rules:
- Verify everything, don't assume, and ask when something is unclear.
- Claude writes all the code; the plugin builds only on GitHub Actions. The JUCE-free core (`source/core`) and its unit tests also compile locally with MSYS2 g++ (command in the plan's Build loop): run them before every push.
- Claude runs Live tests itself with mouse/keyboard (`tools/live/`). Never install Live Remote Scripts or change Live preferences.
- Plugin codes `Mtgm`/`Trsc` in `CMakeLists.txt` must never change.
- Update the Status section of `docs/BUILD_PLAN.md` at the end of each session.
