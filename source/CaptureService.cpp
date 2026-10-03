#include "CaptureService.h"

#include "StateCodec.h"
#include "core/Mei.h"

namespace
{
    const char* stateName (trs::CaptureEngine::State s)
    {
        switch (s)
        {
            case trs::CaptureEngine::State::idle:      return "idle";
            case trs::CaptureEngine::State::armed:     return "armed";
            case trs::CaptureEngine::State::recording: return "recording";
            case trs::CaptureEngine::State::stopped:   return "stopped";
        }

        return "idle";
    }

    const char* modeName (trs::ReadingMode m)
    {
        return m == trs::ReadingMode::oneLoop ? "oneLoop" : "asPlayed";
    }

    const char* sourceName (trs::ReadingSource s)
    {
        switch (s)
        {
            case trs::ReadingSource::host:     return "host";
            case trs::ReadingSource::detected: return "detected";
            case trs::ReadingSource::user:     return "user";
            case trs::ReadingSource::none:     break;
        }

        return "none";
    }

    // The preview is only a picture; very long captures are cut off.
    constexpr size_t maxPreviewNotes = 20000;

    // How often the size of the saved state is worked out again while the document changes.
    constexpr double stateSizeIntervalMs = 1000.0;

    void describe (juce::DynamicObject& o, const trs::RawCapture& raw, const trs::Reading& reading,
                   const trs::Detection& detection, const trs::ResolvedCapture& resolved)
    {
        o.setProperty ("rawNotes", (int) raw.notes.size());
        o.setProperty ("scoreNotes", (int) resolved.notes.size());
        o.setProperty ("segments", (int) raw.segments.size());
        o.setProperty ("bars", (int) raw.bars.size());
        o.setProperty ("passes", resolved.numPasses);
        o.setProperty ("startPpq", raw.startPpq);
        o.setProperty ("endPpq", raw.endOfCapture());
        o.setProperty ("stoppedByUser", raw.stoppedByUser);
        o.setProperty ("incomplete", raw.incomplete);
        o.setProperty ("mode", modeName (reading.mode));
        o.setProperty ("source", sourceName (reading.source));
        o.setProperty ("loopStartPpq", reading.loopStartPpq);
        o.setProperty ("loopLengthPpq", reading.loopLengthPpq);
        o.setProperty ("loopBars", reading.loopLengthPpq > 0.0 ? trs::loopBarsOf (raw, reading) : 0);
        o.setProperty ("candidateBars", detection.candidateBars);
        o.setProperty ("candidateMismatch", detection.candidateMismatch);
    }
}

CaptureService::CaptureService()
    : juce::Thread ("Transcriber capture")
{
    startThread();
}

CaptureService::~CaptureService()
{
    stopThread (2000);
}

void CaptureService::run()
{
    while (! threadShouldExit())
    {
        wait (10);
        drainNow();
    }
}

void CaptureService::drainNow()
{
    const std::lock_guard<std::mutex> lock (mutex);
    engine.drain ([this] (const trs::Record& r)
    {
        model.consume (r);
        addVersionIfRecordingEnded();
    });

    const auto dropped = engine.getDroppedCount();

    if (dropped != lastDropped)
    {
        lastDropped = dropped;
        model.markIncomplete();
    }

}

// Every finished recording that holds notes becomes a version. This runs after every record, so two
// recordings that end within one pass of the drain loop each get their own.
void CaptureService::addVersionIfRecordingEnded()
{
    if (model.recordingsStopped() <= handledStops)
        return;

    handledStops = model.recordingsStopped();
    lastStopWasEmpty = model.raw().notes.empty();

    if (lastStopWasEmpty)
        return;

    document.addVersion (model.raw(), model.reading(), model.detection(), juce::Time::currentTimeMillis());

    // The user has recorded something new: a state that could not be read is no longer protected.
    unreadableState.reset();
    loadMessage = {};
}

//==============================================================================
void CaptureService::setMode (trs::ReadingMode mode)
{
    const std::lock_guard<std::mutex> lock (mutex);

    if (auto* v = document.activeMutable(); v != nullptr && ! v->scoreEdited)
        v->setReading (trs::readingWithMode (v->capture, v->reading, mode));
}

void CaptureService::setLoopBars (int numBars)
{
    const std::lock_guard<std::mutex> lock (mutex);

    if (auto* v = document.activeMutable(); v != nullptr && ! v->scoreEdited)
        v->setReading (trs::readingWithLoopBars (v->capture, v->reading, numBars));
}

void CaptureService::redetect()
{
    const std::lock_guard<std::mutex> lock (mutex);

    if (auto* v = document.activeMutable(); v != nullptr && ! v->scoreEdited)
    {
        v->detection = trs::detectReading (v->capture);
        v->setReading (v->detection.reading);
    }
}

void CaptureService::setTranscriptionSetting (const juce::String& name, const juce::var& value)
{
    const std::lock_guard<std::mutex> lock (mutex);
    auto* v = document.activeMutable();

    if (v == nullptr || v->scoreEdited)   // an edited score is not written again
        return;

    auto s = v->transcriptionSettings();

    if (name == "grid")            s.grid = (int) value;
    else if (name == "triplets")   s.triplets = (bool) value;
    else if (name == "splitPoint") s.splitPoint = (int) value;
    else if (name == "autoPickup") s.autoPickup = (bool) value;
    else if (name == "keyTonic")   s.keyTonic = (int) value;
    else if (name == "keyMinor")   s.keyMinor = (bool) value;
    else if (name == "key")        // tonic * 2 + minor in one step, or -1 to detect the key
    {
        const auto k = (int) value;
        s.keyTonic = k < 0 ? -1 : k / 2;
        s.keyMinor = k >= 0 && k % 2 == 1;
    }
    else return;

    // Out-of-range values are brought back by the settings reader.
    s = trs::TranscriptionSettings::fromJson (s.toJson());

    if (! (s == v->transcriptionSettings()))
        v->setTranscriptionSettings (s);
}

//==============================================================================
trs::DrumMap CaptureService::drumMapById (const std::string& id) const
{
    for (const auto& item : document.drumMaps.items())
    {
        trs::DrumMap map;

        if (trs::DrumMap::fromJson (item, map) && map.id == id)
            return map;
    }

    return trs::drumPreset (id);
}

std::string CaptureService::selectedDrumMapId() const
{
    const auto id = document.uiPrefs.get ("drumMapId").asString();
    return id.empty() ? "gm" : id;
}

trs::Profile CaptureService::profileFor (trs::InstrumentType type) const
{
    trs::Profile p;
    p.type = type;
    p.drumMap = drumMapById (selectedDrumMapId());
    return p;
}

void CaptureService::setInstrument (const juce::String& type)
{
    const std::lock_guard<std::mutex> lock (mutex);
    const auto profile = profileFor (trs::instrumentFromName (type.toStdString()));
    document.defaultProfile = profile.toJson();
    document.markChanged();

    if (auto* v = document.activeMutable(); v != nullptr && ! v->scoreEdited)
        v->setProfile (profile);
}

// The map in use changed: new takes and the shown take (when it is for drums) get the new copy.
void CaptureService::applyDrumMapToProfiles (const trs::DrumMap& map)
{
    if (trs::Profile::fromJson (document.defaultProfile).type == trs::InstrumentType::drums)
    {
        trs::Profile p;
        p.type = trs::InstrumentType::drums;
        p.drumMap = map;
        document.defaultProfile = p.toJson();
    }

    if (auto* v = document.activeMutable(); v != nullptr && ! v->scoreEdited)
    {
        auto p = v->instrument();

        if (p.type == trs::InstrumentType::drums)
        {
            p.drumMap = map;
            v->setProfile (p);
        }
    }

    document.markChanged();
    ++drumMapRevision;
}

void CaptureService::selectDrumMap (const juce::String& id)
{
    const std::lock_guard<std::mutex> lock (mutex);
    const auto map = drumMapById (id.toStdString());
    document.uiPrefs.set ("drumMapId", map.id);
    applyDrumMapToProfiles (map);
}

std::string CaptureService::addUserDrumMap (trs::DrumMap map, bool replaceSameId)
{
    bool builtIn = false;

    for (const auto& preset : trs::drumPresets())
        builtIn = builtIn || preset.id == map.id;

    auto exists = [&] (const std::string& id)
    {
        for (const auto& item : document.drumMaps.items())
            if (item.get ("id").asString() == id)
                return true;

        return false;
    };

    // a built-in id, or an id that is taken when we may not replace, gets a new one
    if (builtIn || (exists (map.id) && ! replaceSameId))
    {
        int n = 1;

        while (exists ("user-" + std::to_string (n)))
            ++n;

        map.id = "user-" + std::to_string (n);
    }

    auto list = trs::Json::array();
    bool replaced = false;

    for (const auto& item : document.drumMaps.items())
    {
        if (item.get ("id").asString() == map.id)
        {
            list.push (map.toJson());
            replaced = true;
        }
        else
        {
            list.push (item);
        }
    }

    if (! replaced)
        list.push (map.toJson());

    document.drumMaps = std::move (list);
    document.uiPrefs.set ("drumMapId", map.id);
    applyDrumMapToProfiles (map);
    return map.id;
}

juce::String CaptureService::saveDrumMap (const juce::String& jsonText)
{
    trs::Json json;
    std::string error;

    if (! trs::Json::parse (jsonText.toStdString(), json, &error))
        return "The drum map could not be read: " + juce::String (error);

    trs::DrumMap map;

    if (! trs::DrumMap::fromJson (json, map, &error))
        return juce::String::fromUTF8 (error.c_str());

    const std::lock_guard<std::mutex> lock (mutex);
    addUserDrumMap (std::move (map), true);
    return {};
}

juce::String CaptureService::importDrumMap (const juce::String& jsonText)
{
    trs::Json json;
    std::string error;

    if (! trs::Json::parse (jsonText.toStdString(), json, &error))
        return "This file is not a drum map (it is not valid JSON).";

    trs::DrumMap map;

    if (! trs::DrumMap::fromJson (json, map, &error))
        return juce::String::fromUTF8 (error.c_str());

    const std::lock_guard<std::mutex> lock (mutex);
    addUserDrumMap (std::move (map), false);
    return {};
}

void CaptureService::deleteDrumMap (const juce::String& id)
{
    const std::lock_guard<std::mutex> lock (mutex);
    auto list = trs::Json::array();
    bool removed = false;

    for (const auto& item : document.drumMaps.items())
    {
        if (item.get ("id").asString() == id.toStdString())
            removed = true;
        else
            list.push (item);
    }

    if (! removed)
        return;

    document.drumMaps = std::move (list);

    if (selectedDrumMapId() == id.toStdString())
    {
        document.uiPrefs.set ("drumMapId", "gm");
        applyDrumMapToProfiles (trs::drumPreset ("gm"));
    }

    document.markChanged();
    ++drumMapRevision;
}

juce::String CaptureService::getDrumMapJson (const juce::String& id) const
{
    const std::lock_guard<std::mutex> lock (mutex);
    const auto map = drumMapById (id.toStdString());
    return juce::String::fromUTF8 (map.toJson().dump().c_str());
}

void CaptureService::selectVersion (const juce::String& id)
{
    const std::lock_guard<std::mutex> lock (mutex);
    lastStopWasEmpty = false;
    document.select (id.toStdString());
}

void CaptureService::renameVersion (const juce::String& id, const juce::String& name)
{
    const std::lock_guard<std::mutex> lock (mutex);
    document.rename (id.toStdString(), name.trim().substring (0, 80).toStdString());
}

void CaptureService::duplicateVersion (const juce::String& id)
{
    const std::lock_guard<std::mutex> lock (mutex);
    lastStopWasEmpty = false;
    document.duplicate (id.toStdString(), juce::Time::currentTimeMillis());
}

void CaptureService::deleteVersion (const juce::String& id)
{
    const std::lock_guard<std::mutex> lock (mutex);
    lastStopWasEmpty = false;
    histories.erase (id.toStdString());
    document.remove (id.toStdString());
}

int CaptureService::getNumVersions() const
{
    const std::lock_guard<std::mutex> lock (mutex);
    return (int) document.versions().size();
}

//==============================================================================
uint64_t CaptureService::getRevisionLocked() const
{
    // Only used to notice that something changed.
    return model.revision() * 1000003ull + document.revision() * 7919ull + (uint64_t) engine.getState();
}

uint64_t CaptureService::getRevision() const
{
    const std::lock_guard<std::mutex> lock (mutex);
    return getRevisionLocked();
}

void CaptureService::updateStateSize() const
{
    const auto revision = document.revision();
    const auto now = juce::Time::getMillisecondCounterHiRes();

    if (revision == sizedRevision || (sizedRevision != ~(uint64_t) 0 && now - sizedAtMs < stateSizeIntervalMs))
        return;

    juce::MemoryBlock block;

    if (document.versions().empty() && unreadableState.getSize() > 0)
        block = unreadableState;
    else
        statecodec::encode (document.toJson().dump(), block);

    stateBytes = block.getSize();
    sizedRevision = revision;
    sizedAtMs = now;
}

juce::var CaptureService::getStatus() const
{
    auto state = engine.getState();
    const auto pending = engine.getPending();
    const std::lock_guard<std::mutex> lock (mutex);
    updateStateSize();

    const auto live = state == trs::CaptureEngine::State::recording;

    // Show a command that the audio thread has not seen yet as if it had taken effect.
    using Pending = trs::CaptureEngine::Pending;
    using State = trs::CaptureEngine::State;

    if (pending == Pending::arm && state != State::recording)
        state = State::armed;
    else if (pending == Pending::reset || (pending == Pending::stop && state == State::armed))
        state = State::idle;

    auto* o = new juce::DynamicObject();
    o->setProperty ("state", stateName (state));
    o->setProperty ("revision", (juce::int64) getRevisionLocked());
    o->setProperty ("dropped", (juce::int64) engine.getDroppedCount());

    const auto* v = document.active();

    if (live)
        describe (*o, model.raw(), model.reading(), model.detection(), model.resolved());
    else if (v != nullptr)
        describe (*o, v->capture, v->reading, v->detection, v->resolved());
    else
        describe (*o, trs::RawCapture {}, trs::Reading {}, trs::Detection {}, trs::ResolvedCapture {});

    juce::Array<juce::var> versions;

    for (const auto& ver : document.versions())
    {
        auto* e = new juce::DynamicObject();
        e->setProperty ("id", juce::String (ver.id));
        e->setProperty ("name", juce::String::fromUTF8 (ver.name.c_str()));
        e->setProperty ("createdAt", (juce::int64) ver.createdAtMs);
        e->setProperty ("notes", (int) ver.capture.notes.size());
        e->setProperty ("beats", ver.capture.endOfCapture() - ver.capture.startPpq);
        e->setProperty ("mode", modeName (ver.reading.mode));
        e->setProperty ("loopBars", ver.reading.loopLengthPpq > 0.0 ? trs::loopBarsOf (ver.capture, ver.reading) : 0);
        e->setProperty ("active", ver.id == document.activeId());
        versions.add (juce::var (e));
    }

    if (! live && v != nullptr)
    {
        const auto settings = v->transcriptionSettings();
        auto* st = new juce::DynamicObject();
        st->setProperty ("grid", settings.grid);
        st->setProperty ("triplets", settings.triplets);
        st->setProperty ("splitPoint", settings.splitPoint);
        st->setProperty ("autoPickup", settings.autoPickup);
        st->setProperty ("keyTonic", settings.keyTonic);
        st->setProperty ("keyMinor", settings.keyMinor);
        o->setProperty ("settings", juce::var (st));

        const auto profile = v->instrument();
        o->setProperty ("instrument", juce::String (trs::instrumentName (profile.type)));

        auto* t = new juce::DynamicObject();
        t->setProperty ("keyTonic", v->key.tonic);
        t->setProperty ("keyMinor", v->key.minor);
        t->setProperty ("keyFifths", v->key.fifths);
        t->setProperty ("keyCorrelation", v->key.correlation);
        t->setProperty ("measures", v->report.measures);
        t->setProperty ("voices", v->report.maxVoices);
        t->setProperty ("offGrid", v->report.offGridNotes);
        t->setProperty ("tripletBeats", v->report.tripletBeats);
        t->setProperty ("edited", v->scoreEdited);

        {
            auto* ed = new juce::DynamicObject();
            const auto it = histories.find (v->id);
            const auto* undo = it != histories.end() ? it->second.get() : nullptr;
            ed->setProperty ("canUndo", undo != nullptr && undo->canUndo());
            ed->setProperty ("canRedo", undo != nullptr && undo->canRedo());
            ed->setProperty ("undoName", undo != nullptr ? juce::String::fromUTF8 (undo->undoName().c_str()) : juce::String());
            ed->setProperty ("redoName", undo != nullptr ? juce::String::fromUTF8 (undo->redoName().c_str()) : juce::String());
            ed->setProperty ("blocked", juce::String::fromUTF8 (trs::editBlocker (v->score).c_str()));
            o->setProperty ("edit", juce::var (ed));
        }

        juce::Array<juce::var> warnings;

        for (const auto& w : v->report.warnings)
            warnings.add (juce::String::fromUTF8 (w.c_str()));

        t->setProperty ("warnings", warnings);
        o->setProperty ("transcription", juce::var (t));

        const auto key = v->id + "#" + std::to_string (v->revision());

        if (key != scoreTextKey)
        {
            scoreTextKey = key;
            scoreTextCache = trs::dumpScore (v->score);

            if (scoreTextCache.size() > 40000)
            {
                scoreTextCache.resize (40000);
                scoreTextCache += "\n... (the rest is not shown)\n";
            }
        }

        o->setProperty ("scoreText", juce::String::fromUTF8 (scoreTextCache.c_str()));
        o->setProperty ("scoreKey", juce::String (key));
    }

    // the instrument of the shown take, or the default for the next one; the drum maps
    if (live || v == nullptr)
        o->setProperty ("instrument", juce::String (trs::instrumentName (trs::Profile::fromJson (document.defaultProfile).type)));

    {
        juce::Array<juce::var> maps;

        auto add = [&] (const std::string& id, const std::string& name, bool builtIn)
        {
            auto* m = new juce::DynamicObject();
            m->setProperty ("id", juce::String (id));
            m->setProperty ("name", juce::String::fromUTF8 (name.c_str()));
            m->setProperty ("builtIn", builtIn);
            maps.add (juce::var (m));
        };

        for (const auto& preset : trs::drumPresets())
            add (preset.id, preset.name, true);

        for (const auto& item : document.drumMaps.items())
            add (item.get ("id").asString(), item.get ("name").asString(), false);

        o->setProperty ("drumMaps", maps);
        o->setProperty ("drumMapId", juce::String (selectedDrumMapId()));
        o->setProperty ("drumMapRevision", (juce::int64) drumMapRevision);
    }

    o->setProperty ("versions", versions);
    o->setProperty ("activeVersion", juce::String (document.activeId()));
    o->setProperty ("stateBytes", (juce::int64) stateBytes);
    o->setProperty ("stateWarnBytes", (juce::int64) statecodec::warnBytes);
    o->setProperty ("stateTooBig", stateBytes >= statecodec::warnBytes);
    o->setProperty ("loadMessage", loadMessage);
    o->setProperty ("lastStopEmpty", lastStopWasEmpty);
    return juce::var (o);
}

juce::var CaptureService::getMei() const
{
    const std::lock_guard<std::mutex> lock (mutex);
    const auto* v = document.active();
    auto* o = new juce::DynamicObject();

    if (v == nullptr || v->score.root().children.empty())
    {
        o->setProperty ("key", juce::String());
        o->setProperty ("mei", juce::String());
        return juce::var (o);
    }

    const auto key = v->id + "#" + std::to_string (v->revision());

    if (key != meiKey)
    {
        trs::MeiOptions options;
        options.title = v->name;
        meiCache = trs::scoreToMei (v->score, options);
        meiKey = key;
    }

    o->setProperty ("key", juce::String (key));
    o->setProperty ("mei", juce::String::fromUTF8 (meiCache.c_str()));

    // the spacing the user chose (0 = the page decides)
    auto* layout = new juce::DynamicObject();
    int systemSpacing = 0, staffSpacing = 0;

    for (const auto& c : v->score.root().children)
        if (c.type == trs::nodeType::layout)
        {
            systemSpacing = (int) c.prop ("systemSpacing").asInt();
            staffSpacing = (int) c.prop ("staffSpacing").asInt();
        }

    layout->setProperty ("systemSpacing", systemSpacing);
    layout->setProperty ("staffSpacing", staffSpacing);
    o->setProperty ("layout", juce::var (layout));
    return juce::var (o);
}

trs::UndoManager& CaptureService::historyOf (trs::Version& v)
{
    auto& slot = histories[v.id];

    if (slot == nullptr)
        slot = std::make_unique<trs::UndoManager> (v.score);
    else
        slot->rebind (v.score);   // the list of versions may have moved the score

    return *slot;
}

juce::var CaptureService::editScore (const juce::var& request)
{
    const std::lock_guard<std::mutex> lock (mutex);
    auto* o = new juce::DynamicObject();
    auto* v = document.activeMutable();
    trs::Json json;

    if (v == nullptr || v->score.root().children.empty()
        || ! trs::Json::parse (juce::JSON::toString (request, true).toStdString(), json))
    {
        o->setProperty ("ok", false);
        o->setProperty ("message", "There is no score to edit.");
        return juce::var (o);
    }

    const auto result = trs::performEdit (v->score, historyOf (*v), json);

    if (result.ok && json.get ("op").asString() != "undo" && json.get ("op").asString() != "redo")
        v->scoreEdited = true;

    if (result.ok)   // the key of the score is what the page shows (a key change, or its undo)
    {
        const auto& root = v->score.root();
        v->key.fifths = (int) root.prop ("keyFifths").asInt();
        v->key.tonic = (int) root.prop ("keyTonic").asInt();
        v->key.minor = root.prop ("keyMode").asString() == "minor";
    }

    o->setProperty ("ok", result.ok);
    o->setProperty ("message", juce::String::fromUTF8 (result.message.c_str()));
    o->setProperty ("select", juce::String::fromUTF8 (result.select.c_str()));
    return juce::var (o);
}

void CaptureService::discardEdits()
{
    const std::lock_guard<std::mutex> lock (mutex);

    if (auto* v = document.activeMutable(); v != nullptr && v->scoreEdited)
    {
        histories.erase (v->id);
        v->discardEdits();
    }
}

juce::String CaptureService::describeScoreNode (const juce::String& id) const
{
    const std::lock_guard<std::mutex> lock (mutex);
    const auto* v = document.active();

    if (v == nullptr)
        return {};

    return juce::String::fromUTF8 (trs::describeNode (v->score, id.toStdString()).c_str());
}

juce::var CaptureService::getPreview() const
{
    const auto live = engine.getState() == trs::CaptureEngine::State::recording;
    const std::lock_guard<std::mutex> lock (mutex);

    trs::ResolvedCapture empty;
    const auto* v = document.active();
    const auto& resolved = live ? model.resolved() : (v != nullptr ? v->resolved() : empty);

    auto* o = new juce::DynamicObject();
    o->setProperty ("revision", (juce::int64) getRevisionLocked());
    o->setProperty ("length", resolved.lengthPpq);

    juce::Array<juce::var> notes;
    const auto count = std::min (resolved.notes.size(), maxPreviewNotes);
    notes.ensureStorageAllocated ((int) count * 5);

    // Five numbers per note: on, off, pitch, velocity, pass.
    for (size_t i = 0; i < count; ++i)
    {
        const auto& n = resolved.notes[i];
        notes.add (n.onPpq);
        notes.add (n.offPpq);
        notes.add (n.pitch);
        notes.add (n.velocity);
        notes.add (n.pass);
    }

    juce::Array<juce::var> bars;

    for (const auto& b : resolved.bars)
    {
        bars.add (b.ppq);
        bars.add (b.num);
        bars.add (b.den);
    }

    o->setProperty ("notes", notes);
    o->setProperty ("bars", bars);
    o->setProperty ("truncated", resolved.notes.size() > count);
    return juce::var (o);
}

std::vector<CapturedNote> CaptureService::getResolvedNotes() const
{
    const std::lock_guard<std::mutex> lock (mutex);
    std::vector<CapturedNote> out;
    const auto* v = document.active();

    if (v == nullptr)
        return out;

    for (const auto& n : v->resolved().notes)
        out.push_back ({ n.onPpq, n.offPpq, n.pitch, n.velocity, n.channel });

    return out;
}

//==============================================================================
void CaptureService::saveState (juce::MemoryBlock& destination) const
{
    const std::lock_guard<std::mutex> lock (mutex);

    // A state we could not read goes back exactly as it came, until something new is recorded.
    if (document.versions().empty() && unreadableState.getSize() > 0)
    {
        destination = unreadableState;
        return;
    }

    destination.reset();
    statecodec::encode (document.toJson().dump(), destination);
}

void CaptureService::loadState (const void* data, size_t size)
{
    std::string json;
    const auto decoded = statecodec::decode (data, size, json);

    const std::lock_guard<std::mutex> lock (mutex);
    document = trs::Document();
    histories.clear();
    unreadableState.reset();
    loadMessage = {};

    auto keepUnreadable = [&] (const juce::String& why)
    {
        unreadableState.replaceAll (data, size);
        loadMessage = why;
    };

    if (decoded == statecodec::Decoded::notOurs)
    {
        if (size > 4)
            loadMessage = "The saved state was written by an older test build and was ignored.";

        return;
    }

    if (decoded == statecodec::Decoded::damaged)
    {
        keepUnreadable ("The saved state is damaged and could not be loaded. It is kept unchanged until you record something new.");
        return;
    }

    trs::Json parsed;
    std::string error;

    if (! trs::Json::parse (json, parsed, &error))
    {
        keepUnreadable ("The saved state could not be read (" + juce::String (error) + "). It is kept unchanged until you record something new.");
        return;
    }

    trs::Document loaded;
    const auto result = trs::Document::fromJson (parsed, loaded, &error);

    if (result == trs::LoadResult::ok)
    {
        document = std::move (loaded);
        return;
    }

    keepUnreadable (result == trs::LoadResult::tooNew
                        ? juce::String ("The saved state was written by a newer version of Transcriber. It is kept unchanged until you record something new.")
                        : "The saved state could not be loaded (" + juce::String (error) + "). It is kept unchanged until you record something new.");
}
