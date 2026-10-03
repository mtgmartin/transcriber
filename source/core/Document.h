#pragma once

#include "CaptureTypes.h"
#include "Json.h"
#include "Notation.h"
#include "Reading.h"
#include "Score.h"

#include <functional>
#include <string>
#include <vector>

namespace trs
{

// RawCapture, Reading and Detection as JSON. Notes, segments, bar lines and tempo points are
// stored as flat arrays of numbers to keep long recordings small.
Json captureToJson (const RawCapture&);
bool captureFromJson (const Json&, RawCapture& result, std::string* error = nullptr);

Json readingToJson (const Reading&);
Reading readingFromJson (const Json&);

Json transcriptionToJson (const KeyResult&, const TranscriptionReport&);
void transcriptionFromJson (const Json&, KeyResult&, TranscriptionReport&);

Json detectionToJson (const Detection&);
Detection detectionFromJson (const Json&);

// One recording and everything made from it. The raw capture is never edited; only the reading
// (One loop / As played) and the score change.
class Version
{
public:
    std::string id;
    std::string name;
    int64_t createdAtMs = 0;       // milliseconds since 1970 (UTC)
    Json profile = Json::object(); // the instrument as it was when recording, e.g. {"type":"piano"}
    Json settings = Json::object();// transcription settings (quantisation grid, split point, ...)
    RawCapture capture;
    Reading reading;
    Detection detection;
    Score score;

    // What the transcription made of the recording (the key and the warnings), kept with the score.
    KeyResult key;
    TranscriptionReport report;

    // True once the user has changed the score; the transcription then leaves it alone.
    bool scoreEdited = false;

    // The notes after the reading is applied; kept until the reading changes.
    const ResolvedCapture& resolved() const;

    // Changing the reading or the settings writes the score again, unless it has been edited.
    void setReading (const Reading&);
    TranscriptionSettings transcriptionSettings() const { return TranscriptionSettings::fromJson (settings); }
    void setTranscriptionSettings (const TranscriptionSettings&);

    // The instrument the score is written for; a copy of its drum map is kept with the version.
    Profile instrument() const { return Profile::fromJson (profile); }
    void setProfile (const Profile&);

    // Makes the score from the recording for the profile. Does nothing for an edited score.
    void regenerate();

    // Throws the user's edits away: the score is made from the recording again.
    void discardEdits();

    // Increases when the reading or the score changes.
    uint64_t revision() const noexcept { return readingRevision + score.revision(); }

private:
    friend class Document;
    mutable ResolvedCapture cache;
    mutable bool cacheValid = false;
    uint64_t readingRevision = 0;
};

// A step that upgrades a saved document from schema version `index` to `index + 1`.
using Migration = std::function<bool (Json& document, std::string* error)>;

// Upgrades `document` to `targetSchema` using steps[i] for i -> i+1. false if the document is
// newer than the target, or a step fails.
bool migrateDocument (Json& document, int targetSchema, const std::vector<Migration>& steps, std::string* error = nullptr);

enum class LoadResult { ok, tooNew, invalid };

// All the plugin keeps between sessions.
class Document
{
public:
    static constexpr int currentSchema = 1;

    const std::vector<Version>& versions() const noexcept { return list; }
    const Version* find (const std::string& id) const;
    Version* findMutable (const std::string& id);
    const Version* active() const;
    Version* activeMutable();
    const std::string& activeId() const noexcept { return activeVersionId; }

    // A new version from a finished recording; it becomes the active one. An empty name gives "Take N".
    std::string addVersion (const RawCapture&, const Reading&, const Detection&, int64_t nowMs, const std::string& name = {});

    bool select (const std::string& id);
    bool rename (const std::string& id, const std::string& newName);

    // A copy of the version (with its score) named "<name> copy"; the copy becomes active. Returns its id.
    std::string duplicate (const std::string& id, int64_t nowMs);

    // Removes a version. If it was the active one, the neighbour before it (or the first) takes over.
    bool remove (const std::string& id);

    Json uiPrefs = Json::object();
    Json drumMaps = Json::array();   // the drum maps the user made (the built-in ones are in code)
    Json defaultProfile = Json::object();   // the instrument a new recording gets

    // Changes whenever a version is added, removed, renamed, selected or edited.
    uint64_t revision() const;

    // Counts a change that is not in a version (the default instrument, the drum maps), so that it is saved.
    void markChanged() { ++structureRevision; }

    Json toJson() const;
    // `targetSchema` and `steps` exist so that tests can exercise the migration hook; the plugin uses the defaults.
    static LoadResult fromJson (const Json&, Document& result, std::string* error = nullptr,
                                int targetSchema = currentSchema, const std::vector<Migration>& steps = {});

private:
    std::vector<Version> list;
    std::string activeVersionId;
    int nextTake = 1;
    uint64_t structureRevision = 0;
    int64_t idCounter = 1;
    std::string makeId();
};

}  // namespace trs
