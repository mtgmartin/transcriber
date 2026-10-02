#pragma once

#include <juce_core/juce_core.h>

#include "MidiCompare.h"
#include "core/CaptureEngine.h"
#include "core/CaptureModel.h"
#include "core/Document.h"

#include <algorithm>
#include <mutex>
#include <string>
#include <vector>

// Connects the audio thread's CaptureEngine to what the plugin keeps: a background thread
// drains the engine's ring into the CaptureModel, every finished recording becomes a Version in
// the Document, and the message thread reads and edits the Document through the same mutex.
class CaptureService final : private juce::Thread
{
public:
    CaptureService();
    ~CaptureService() override;

    trs::CaptureEngine& getEngine() noexcept { return engine; }

    // Record button: arms, or (while recording) stops. Takes effect at the next audio block.
    void arm()  { engine.requestArm(); }
    void stop() { engine.requestStop(); }

    // The reading of the version that is shown
    void setMode (trs::ReadingMode);
    void setLoopBars (int numBars);
    void redetect();

    // The transcription settings of the version that is shown: "grid" (4, 8, 16, 32), "triplets",
    // "splitPoint", "autoPickup", "keyTonic" (-1 = detect, else 0-11) and "keyMinor".
    void setTranscriptionSetting (const juce::String& name, const juce::var& value);

    // Versions
    void selectVersion (const juce::String& id);
    void renameVersion (const juce::String& id, const juce::String& name);
    void duplicateVersion (const juce::String& id);
    void deleteVersion (const juce::String& id);

    // For the UI
    juce::var getStatus() const;
    juce::var getPreview() const;
    uint64_t getRevision() const;

    // The notes the score is built from, for the MIDI-file comparison.
    std::vector<CapturedNote> getResolvedNotes() const;

    // Saved state (called by the host)
    void saveState (juce::MemoryBlock&) const;
    void loadState (const void* data, size_t size);

    int getNumVersions() const;

private:
    void run() override;
    void drainNow();
    void addVersionIfRecordingEnded();   // the mutex must be held
    void updateStateSize() const;
    uint64_t getRevisionLocked() const;   // the mutex must be held

    trs::CaptureEngine engine;
    mutable std::mutex mutex;
    trs::CaptureModel model;
    trs::Document document;
    uint32_t lastDropped = 0;
    uint64_t handledStops = 0;
    bool lastStopWasEmpty = false;

    // A saved state that could not be read (damaged, or from a newer Transcriber) is kept as it
    // was and written back unchanged until the user records something new.
    juce::MemoryBlock unreadableState;
    juce::String loadMessage;

    // The score as text for the page, worked out again only when the version changes.
    mutable std::string scoreTextKey, scoreTextCache;

    mutable uint64_t sizedRevision = ~(uint64_t) 0;
    mutable double sizedAtMs = 0.0;
    mutable size_t stateBytes = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CaptureService)
};
