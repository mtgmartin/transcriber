#pragma once

#include <juce_core/juce_core.h>

#include "MidiCompare.h"
#include "core/CaptureEngine.h"
#include "core/CaptureModel.h"
#include "core/Commands.h"
#include "core/Document.h"
#include "core/Edit.h"

#include <algorithm>
#include <map>
#include <memory>
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

    // The instrument of the version that is shown ("piano", "drums", "guitar", "bass"); new recordings get it too.
    void setInstrument (const juce::String& type);

    // The strings of a guitar or bass score: "auto", "preset:<id>", or notes typed by the user ("D2 A2 D3 G3 B3 E4", MEI numbers). What is
    // wrong is said in the status ("tuningMessage"). On a score that was edited the tuning is changed as an edit.
    void setTuning (const juce::String& request);

    // Drum maps. The built-in ones ("gm", "gm2") cannot be changed; saving a changed built-in map makes a copy.
    // The functions that can fail give an empty string on success, else the reason.
    void selectDrumMap (const juce::String& id);
    juce::String saveDrumMap (const juce::String& jsonText);      // the map as JSON; selects it
    juce::String importDrumMap (const juce::String& jsonText);    // a map from a file; selects it
    void deleteDrumMap (const juce::String& id);
    juce::String getDrumMapJson (const juce::String& id) const;   // an unknown id gives the General MIDI map

    // Versions
    void selectVersion (const juce::String& id);
    void renameVersion (const juce::String& id, const juce::String& name);

    // The title and composer written above the score of the active take (page and PDF).
    void setScoreMeta (const juce::String& title, const juce::String& composer);
    void duplicateVersion (const juce::String& id);
    void deleteVersion (const juce::String& id);

    // For the UI
    juce::var getStatus() const;
    juce::var getPreview() const;

    // The score of the version that is shown, as MEI for the engraving library: { key, mei } (mei is
    // empty when there is no score). The key changes whenever the score does.
    juce::var getMei() const;

    // One edit of the score of the version that is shown (see core/Edit.h for the requests): { ok, message, select }.
    // The first edit marks the score as edited, so that changing the reading or the settings no longer writes it again.
    juce::var editScore (const juce::var& request);

    // Throws the edits of the shown version away: the score is made from the recording again.
    void discardEdits();

    // What a click on a note, rest or chord of the engraved score should say.
    juce::String describeScoreNode (const juce::String& id) const;
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

    // The mutex must be held for these.
    trs::DrumMap drumMapById (const std::string& id) const;
    std::string selectedDrumMapId() const;
    trs::Profile profileFor (trs::InstrumentType) const;
    void applyDrumMapToProfiles (const trs::DrumMap&);
    std::string addUserDrumMap (trs::DrumMap, bool replaceSameId);

    uint64_t drumMapRevision = 0;

    // The undo history of each edited version (not saved with the document).
    std::map<std::string, std::unique_ptr<trs::UndoManager>> histories;
    trs::UndoManager& historyOf (trs::Version&);

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
    juce::String tuningMessage;   // why the last tuning request was refused (empty: it was done)
    mutable std::string meiKey, meiCache;

    mutable uint64_t sizedRevision = ~(uint64_t) 0;
    mutable double sizedAtMs = 0.0;
    mutable size_t stateBytes = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CaptureService)
};
