#pragma once

#include <juce_core/juce_core.h>

#include "MidiCompare.h"
#include "core/CaptureEngine.h"
#include "core/CaptureModel.h"

#include <algorithm>
#include <mutex>
#include <vector>

// Connects the audio thread's CaptureEngine to the CaptureModel: a background thread drains
// the engine's ring, and the message thread reads the model through the same mutex.
class CaptureService final : private juce::Thread
{
public:
    CaptureService();
    ~CaptureService() override;

    trs::CaptureEngine& getEngine() noexcept { return engine; }

    // Record button: arms, or (while recording) stops. Takes effect at the next audio block.
    void arm()  { engine.requestArm(); }
    void stop() { engine.requestStop(); }

    // Forgets the capture.
    void clear();

    // The reading
    void setMode (trs::ReadingMode);
    void setLoopBars (int numBars);
    void redetect();

    // For the UI
    juce::var getStatus() const;
    juce::var getPreview() const;
    uint64_t getRevision() const;

    // The notes the score is built from, for the MIDI-file comparison.
    std::vector<CapturedNote> getResolvedNotes() const;

private:
    void run() override;
    void drainNow();

    trs::CaptureEngine engine;
    mutable std::mutex mutex;
    trs::CaptureModel model;
    uint32_t lastDropped = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CaptureService)
};
