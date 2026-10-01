#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <mutex>
#include <vector>

// Diagnostics: records everything the host sends, as JSON Lines. Off by default; the file is only
// created once logging is switched on.
// The audio thread only copies fixed-size records into a lock-free FIFO;
// a background thread formats them and writes the file.
class DiagnosticLog final : private juce::Thread
{
public:
    enum Flags : uint32_t
    {
        playing       = 1u << 0,
        looping       = 1u << 1,
        recording     = 1u << 2,
        nonRealtime   = 1u << 3,
        hasPpq        = 1u << 4,
        hasBpm        = 1u << 5,
        hasTimeSig    = 1u << 6,
        hasLastBarPpq = 1u << 7,
        hasBarCount   = 1u << 8,
        hasLoop       = 1u << 9,
        hasTimeSamples = 1u << 10,
        hasHostTime   = 1u << 11,
        hasPosition   = 1u << 12   // getPosition() returned a value at all
    };

    struct BlockInfo
    {
        uint32_t flags = 0;
        double ppq = 0.0, bpm = 0.0, lastBarPpq = 0.0, loopStart = 0.0, loopEnd = 0.0;
        int64_t timeInSamples = 0, barCount = 0;
        uint64_t hostTimeNs = 0;
        int tsNum = 0, tsDen = 0;

        bool sameTransportAs (const BlockInfo& o) const noexcept;
    };

    struct Record
    {
        enum class Kind : uint8_t { block, midi };

        Kind kind = Kind::block;
        uint64_t blockIndex = 0;
        double wallMs = 0.0;
        int numSamples = 0;
        BlockInfo info;

        // MIDI records only
        int sampleOffset = 0;
        uint8_t midi[3] {};
        int midiSize = 0;
        bool hasEventPpq = false;
        double eventPpq = 0.0;
    };

    explicit DiagnosticLog (const juce::String& instanceId);
    ~DiagnosticLog() override;

    // Audio thread: real-time safe (no allocation, no locks).
    void push (const Record&) noexcept;

    // Any non-audio thread.
    void logEvent (const juce::String& type, const juce::var& data = {});

    void setEnabled (bool shouldLog) noexcept { enabled.store (shouldLog); }
    bool isEnabled() const noexcept { return enabled.load(); }

    juce::File getFile() const { return file; }
    int getDroppedCount() const noexcept { return dropped.load(); }
    int64_t getLinesWritten() const noexcept { return linesWritten.load(); }

private:
    void run() override;
    void drain();
    void writeLine (const juce::var& object);
    void openStream();

    static juce::var toVar (const Record&);

    juce::File file;
    std::unique_ptr<juce::FileOutputStream> stream;   // writer thread only
    bool streamFailed = false;

    static constexpr int capacity = 1 << 15;
    juce::AbstractFifo fifo { capacity };
    std::vector<Record> buffer;
    std::atomic<bool> enabled { false };
    std::atomic<int> dropped { 0 };
    std::atomic<int64_t> linesWritten { 0 };

    std::mutex eventMutex;
    std::vector<juce::var> pendingEvents;
};
