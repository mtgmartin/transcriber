#pragma once

#include <cstdint>
#include <vector>

// The capture core has no JUCE dependency, so it can be unit-tested with any C++17
// compiler. All positions are in quarter notes ("ppq").
//
// Two time bases are used:
//  - song time: what the host reports (jumps back when an Arrangement loop wraps);
//  - capture time: song time of the first recorded block, then continuous. A backward
//    jump does not move capture time back, so every pass of a loop gets its own span.
namespace trs
{

// One audio block as reported by the host.
struct HostBlock
{
    bool playing = false, looping = false, nonRealtime = false;
    bool hasPpq = false, hasBpm = false, hasTimeSig = false, hasBarStart = false;
    double ppq = 0.0;          // song position at the first sample of the block
    double bpm = 0.0;
    double barStartPpq = 0.0;  // song position of the start of the current bar
    int tsNum = 4, tsDen = 4;
    int numSamples = 0;
    double sampleRate = 0.0;
};

struct MidiEvent
{
    int sampleOffset = 0;
    uint8_t data[3] {};
    int size = 0;
};

// What the audio thread hands to the rest of the plugin. Plain data only.
struct Record
{
    enum class Kind : uint8_t { start, block, midi, stop };

    enum Flags : uint8_t
    {
        looping     = 1,
        hasTimeSig  = 2,
        hasBarStart = 4,
        stopFlush   = 8,    // midi: sent by the host while stopping (note-offs, All Notes Off)
        userStop    = 16    // stop: the user pressed Stop; the transport was still running
    };

    Kind kind = Kind::block;
    uint8_t flags = 0;
    uint8_t midiSize = 0;
    uint8_t midi[3] {};
    int numSamples = 0;
    int tsNum = 4, tsDen = 4;
    double ppq = 0.0;          // block start; midi: event position; stop: stop position
    double bpm = 0.0;
    double barStartPpq = 0.0;
    double sampleRate = 0.0;
};

struct RawNote
{
    double onPpq = 0.0;
    double offPpq = -1.0;      // < 0 while the note is still held
    int pitch = 0;
    int velocity = 0;
    int channel = 1;
    bool heldAtStop = false;   // the recording ended while the note was sounding
};

// A stretch of continuous song time. A new segment starts at every jump of the position.
struct Segment
{
    double songStartPpq = 0.0, songEndPpq = 0.0;
    double capStartPpq = 0.0, capEndPpq = 0.0;
    bool wrap = false;         // began with a backward jump while the host's looping flag was on
};

struct TempoPoint
{
    double ppq = 0.0;
    double bpm = 0.0;
};

// A bar line as reported by the host, with the meter in force from there.
struct BarStart
{
    double ppq = 0.0;
    int num = 4, den = 4;

    double length() const noexcept { return den > 0 ? 4.0 * num / den : 4.0; }
};

struct RawCapture
{
    std::vector<RawNote> notes;       // in order of onPpq
    std::vector<Segment> segments;
    std::vector<TempoPoint> tempoMap;
    std::vector<BarStart> bars;       // the meter map: one entry per bar line the host reported

    double startPpq = 0.0;            // capture time of the first block
    double endPpq = 0.0;              // end of the latest block (capture time)
    double stopPpq = 0.0;             // valid when stopped
    double sampleRate = 0.0;
    bool recording = false;
    bool stopped = false;
    bool stoppedByUser = false;
    bool incomplete = false;          // records were lost; the capture has gaps

    double endOfCapture() const noexcept { return stopped ? stopPpq : endPpq; }
};

}  // namespace trs
