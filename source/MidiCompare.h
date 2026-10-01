#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <vector>

struct CapturedNote
{
    double onPpq = 0.0;
    double offPpq = -1.0;   // < 0 while the note is still held
    int pitch = 0;
    int velocity = 0;
    int channel = 1;
};

struct CompareResult
{
    bool passed = false;
    juce::var report;   // JSON-friendly summary for the UI and the log
};

// Aligns the first note of the capture with the first note of the file,
// then matches notes by pitch and onset within the tolerance (quarter notes).
CompareResult compareWithMidiFile (const juce::File& midiFile,
                                   const std::vector<CapturedNote>& captured,
                                   double toleranceQuarterNotes);
