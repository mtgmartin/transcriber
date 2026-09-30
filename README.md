# Transcriber

A VST3 plugin for Ableton Live 11 (Windows) that records the MIDI played on its track and turns it into editable sheet music for piano, drum kit or guitar/bass, with PDF export. Editing the score never changes the MIDI.

**Status:** Phase 1 diagnostic build. The plugin logs everything Live sends it, renders three Verovio test scores in a WebView2 window, and tests PDF export, keyboard input and saved state. See [docs/phase1-tests.md](docs/phase1-tests.md).

## Build

Builds run on GitHub Actions (`.github/workflows/build.yml`): CMake + Visual Studio 2022 on `windows-2022`, validated with [pluginval](https://github.com/Tracktion/pluginval) at strictness 5. JUCE 9.0.3, the WebView2 SDK and the web libraries (Verovio 6.3.0, jsPDF 4.2.1, svg2pdf.js 2.8.1) are downloaded at configure time and checked against their published hashes (`cmake/Dependencies.cmake`).

To build locally instead (needs Visual Studio 2022 with C++ tools and CMake 3.22+):

```
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

## Install

From an administrator PowerShell, with Ableton Live closed:

```
powershell -ExecutionPolicy Bypass -File scripts\install.ps1
```

This downloads the latest successful build and copies `Transcriber.vst3` to `C:\Program Files\Common Files\VST3`.

## Use in Live 11

Transcriber is an instrument, so place it next to your real instrument:

- **Instrument Rack:** group your instrument (or Drum Rack) into an Instrument Rack and load Transcriber on a second chain. Leave that chain's key and velocity zones at full range.
- **MIDI To:** put Transcriber on its own MIDI track, and route the source track's *MIDI To* to that track, choosing Transcriber in the lower menu.

## Licences

JUCE is used under its personal/Starter terms or AGPLv3. The VST3 SDK (bundled with JUCE) is MIT-licensed.
