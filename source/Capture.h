#pragma once

#include "DiagnosticLog.h"

#include <array>
#include <mutex>
#include <vector>

struct CapturedNote
{
    double onPpq = 0.0;
    double offPpq = -1.0;   // < 0 while the note is still held
    int pitch = 0;
    int velocity = 0;
    int channel = 1;
};

// Notes built from the MIDI records, in quarter notes of song time.
// Fed from the log writer thread; read from the message thread.
class Capture
{
public:
    Capture();

    void add (const DiagnosticLog::Record&);
    void clear();

    std::vector<CapturedNote> snapshot() const;
    int size() const;

private:
    mutable std::mutex mutex;
    std::vector<CapturedNote> notes;
    std::array<std::array<int, 128>, 16> openIndex {};   // index into notes, or -1
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
