#include "Document.h"

#include <algorithm>
#include <set>

namespace trs
{

namespace
{
    constexpr size_t noteStride = 6, segmentStride = 5, tempoStride = 2, barStride = 3;

    bool failWith (std::string* error, const std::string& why)
    {
        if (error != nullptr)
            *error = why;

        return false;
    }

    // Reads a flat number array and checks that it divides into whole records.
    bool readFlat (const Json& j, size_t stride, std::vector<double>& out, const char* what, std::string* error)
    {
        if (j.isNull())
        {
            out.clear();
            return true;
        }

        if (! j.isArray() || j.size() % stride != 0)
            return failWith (error, std::string ("the saved ") + what + " are damaged");

        out.resize (j.size());

        for (size_t i = 0; i < out.size(); ++i)
            out[i] = j.numberAt (i);

        return true;
    }
}

Json captureToJson (const RawCapture& c)
{
    auto j = Json::object();
    j.set ("startPpq", c.startPpq);
    j.set ("endPpq", c.endPpq);
    j.set ("stopPpq", c.stopPpq);
    j.set ("sampleRate", c.sampleRate);
    j.set ("stopped", c.stopped);
    j.set ("stoppedByUser", c.stoppedByUser);
    j.set ("incomplete", c.incomplete);

    std::vector<double> notes;
    notes.reserve (c.notes.size() * noteStride);

    for (const auto& n : c.notes)
    {
        notes.push_back (n.onPpq);
        notes.push_back (n.offPpq);
        notes.push_back (n.pitch);
        notes.push_back (n.velocity);
        notes.push_back (n.channel);
        notes.push_back (n.heldAtStop ? 1.0 : 0.0);
    }

    std::vector<double> segments;

    for (const auto& s : c.segments)
    {
        segments.push_back (s.songStartPpq);
        segments.push_back (s.songEndPpq);
        segments.push_back (s.capStartPpq);
        segments.push_back (s.capEndPpq);
        segments.push_back (s.wrap ? 1.0 : 0.0);
    }

    std::vector<double> tempo;

    for (const auto& t : c.tempoMap)
    {
        tempo.push_back (t.ppq);
        tempo.push_back (t.bpm);
    }

    std::vector<double> bars;

    for (const auto& b : c.bars)
    {
        bars.push_back (b.ppq);
        bars.push_back (b.num);
        bars.push_back (b.den);
    }

    j.set ("notes", Json::numbers (std::move (notes)));
    j.set ("segments", Json::numbers (std::move (segments)));
    j.set ("tempo", Json::numbers (std::move (tempo)));
    j.set ("bars", Json::numbers (std::move (bars)));
    return j;
}

bool captureFromJson (const Json& j, RawCapture& result, std::string* error)
{
    if (! j.isObject())
        return failWith (error, "the saved recording is missing");

    RawCapture c;
    c.startPpq = j.get ("startPpq").asDouble();
    c.endPpq = j.get ("endPpq").asDouble();
    c.stopPpq = j.get ("stopPpq").asDouble();
    c.sampleRate = j.get ("sampleRate").asDouble();
    c.stopped = j.get ("stopped").asBool();
    c.stoppedByUser = j.get ("stoppedByUser").asBool();
    c.incomplete = j.get ("incomplete").asBool();

    std::vector<double> flat;

    if (! readFlat (j.get ("notes"), noteStride, flat, "notes", error))
        return false;

    c.notes.reserve (flat.size() / noteStride);

    for (size_t i = 0; i < flat.size(); i += noteStride)
    {
        RawNote n;
        n.onPpq = flat[i];
        n.offPpq = flat[i + 1];
        n.pitch = (int) flat[i + 2];
        n.velocity = (int) flat[i + 3];
        n.channel = (int) flat[i + 4];
        n.heldAtStop = flat[i + 5] != 0.0;

        if (n.pitch < 0 || n.pitch > 127)
            return failWith (error, "a saved note has a pitch outside 0-127");

        c.notes.push_back (n);
    }

    if (! readFlat (j.get ("segments"), segmentStride, flat, "segments", error))
        return false;

    for (size_t i = 0; i < flat.size(); i += segmentStride)
        c.segments.push_back ({ flat[i], flat[i + 1], flat[i + 2], flat[i + 3], flat[i + 4] != 0.0 });

    if (! readFlat (j.get ("tempo"), tempoStride, flat, "tempo changes", error))
        return false;

    for (size_t i = 0; i < flat.size(); i += tempoStride)
        c.tempoMap.push_back ({ flat[i], flat[i + 1] });

    if (! readFlat (j.get ("bars"), barStride, flat, "bar lines", error))
        return false;

    for (size_t i = 0; i < flat.size(); i += barStride)
        c.bars.push_back ({ flat[i], (int) flat[i + 1], (int) flat[i + 2] });

    result = std::move (c);
    return true;
}

Json readingToJson (const Reading& r)
{
    auto j = Json::object();
    j.set ("mode", r.mode == ReadingMode::oneLoop ? "oneLoop" : "asPlayed");
    j.set ("loopStartPpq", r.loopStartPpq);
    j.set ("loopLengthPpq", r.loopLengthPpq);

    const char* source = "none";

    switch (r.source)
    {
        case ReadingSource::host:     source = "host"; break;
        case ReadingSource::detected: source = "detected"; break;
        case ReadingSource::user:     source = "user"; break;
        case ReadingSource::none:     break;
    }

    j.set ("source", source);
    return j;
}

Reading readingFromJson (const Json& j)
{
    Reading r;
    r.mode = j.get ("mode").asString() == "oneLoop" ? ReadingMode::oneLoop : ReadingMode::asPlayed;
    r.loopStartPpq = j.get ("loopStartPpq").asDouble();
    r.loopLengthPpq = j.get ("loopLengthPpq").asDouble();

    const auto& source = j.get ("source").asString();
    r.source = source == "host" ? ReadingSource::host
             : source == "detected" ? ReadingSource::detected
             : source == "user" ? ReadingSource::user
             : ReadingSource::none;
    return r;
}

Json detectionToJson (const Detection& d)
{
    auto j = Json::object();
    j.set ("reading", readingToJson (d.reading));
    j.set ("candidateMismatch", d.candidateMismatch);
    j.set ("candidateBars", d.candidateBars);
    return j;
}

Detection detectionFromJson (const Json& j)
{
    Detection d;
    d.reading = readingFromJson (j.get ("reading"));
    d.candidateMismatch = j.get ("candidateMismatch").asDouble (1.0);
    d.candidateBars = (int) j.get ("candidateBars").asInt();
    return d;
}

Json transcriptionToJson (const KeyResult& key, const TranscriptionReport& report)
{
    auto j = Json::object();

    auto k = Json::object();
    k.set ("tonic", key.tonic);
    k.set ("minor", key.minor);
    k.set ("fifths", key.fifths);
    k.set ("correlation", key.correlation);
    j.set ("key", std::move (k));

    auto r = Json::object();
    r.set ("notes", report.notes);
    r.set ("offGridNotes", report.offGridNotes);
    r.set ("tripletBeats", report.tripletBeats);
    r.set ("mergedNotes", report.mergedNotes);
    r.set ("measures", report.measures);
    r.set ("maxVoices", report.maxVoices);

    auto warnings = Json::array();

    for (const auto& w : report.warnings)
        warnings.push (w);

    r.set ("warnings", std::move (warnings));
    j.set ("report", std::move (r));
    return j;
}

void transcriptionFromJson (const Json& j, KeyResult& key, TranscriptionReport& report)
{
    key = {};
    report = {};
    const auto& k = j.get ("key");
    key.tonic = (int) k.get ("tonic").asInt();
    key.minor = k.get ("minor").asBool();
    key.fifths = (int) k.get ("fifths").asInt();
    key.correlation = k.get ("correlation").asDouble();

    const auto& r = j.get ("report");
    report.notes = (int) r.get ("notes").asInt();
    report.offGridNotes = (int) r.get ("offGridNotes").asInt();
    report.tripletBeats = (int) r.get ("tripletBeats").asInt();
    report.mergedNotes = (int) r.get ("mergedNotes").asInt();
    report.measures = (int) r.get ("measures").asInt();
    report.maxVoices = (int) r.get ("maxVoices").asInt();

    for (const auto& w : r.get ("warnings").items())
        report.warnings.push_back (w.asString());
}

//==============================================================================
const ResolvedCapture& Version::resolved() const
{
    if (! cacheValid)
    {
        cache = resolve (capture, reading);
        cacheValid = true;
    }

    return cache;
}

void Version::setReading (const Reading& r)
{
    reading = r;
    cacheValid = false;
    ++readingRevision;
    regenerate();
}

void Version::setProfile (const Profile& p)
{
    profile = p.toJson();
    ++readingRevision;
    regenerate();
}

void Version::setTranscriptionSettings (const TranscriptionSettings& s)
{
    settings = s.toJson();
    ++readingRevision;
    regenerate();
}

void Version::discardEdits()
{
    scoreEdited = false;
    ++readingRevision;   // the score is a new one: whoever shows it draws it again
    regenerate();
}

void Version::regenerate()
{
    if (scoreEdited)
        return;

    auto result = transcribe (resolved(), transcriptionSettings(), instrument());
    score = std::move (result.score);
    key = result.key;
    report = std::move (result.report);
}

//==============================================================================
bool migrateDocument (Json& document, int targetSchema, const std::vector<Migration>& steps, std::string* error)
{
    if (! document.isObject() || ! document.get ("schemaVersion").isNumber())
        return failWith (error, "the saved state has no schema version");

    auto version = (int) document.get ("schemaVersion").asInt();

    if (version > targetSchema)
        return failWith (error, "the saved state was written by a newer version of Transcriber");

    while (version < targetSchema)
    {
        if (version < 1 || (size_t) version > steps.size() || ! steps[(size_t) version - 1])
            return failWith (error, "there is no way to upgrade the saved state from version " + std::to_string (version));

        if (! steps[(size_t) version - 1] (document, error))
            return false;

        ++version;
        document.set ("schemaVersion", version);
    }

    return true;
}

//==============================================================================
const Version* Document::find (const std::string& id) const
{
    for (const auto& v : list)
        if (v.id == id)
            return &v;

    return nullptr;
}

Version* Document::findMutable (const std::string& id)
{
    for (auto& v : list)
        if (v.id == id)
            return &v;

    return nullptr;
}

const Version* Document::active() const        { return find (activeVersionId); }
Version* Document::activeMutable()             { return findMutable (activeVersionId); }

std::string Document::makeId()
{
    for (;;)
    {
        auto id = "ver-" + std::to_string (idCounter++);

        if (find (id) == nullptr)
            return id;
    }
}

std::string Document::addVersion (const RawCapture& capture, const Reading& reading, const Detection& detection,
                                  int64_t nowMs, const std::string& name)
{
    Version v;
    v.id = makeId();
    v.name = name.empty() ? "Take " + std::to_string (nextTake) : name;
    ++nextTake;
    v.createdAtMs = nowMs;
    v.capture = capture;
    v.capture.recording = false;
    v.reading = reading;
    v.detection = detection;
    v.profile = defaultProfile.isObject() && defaultProfile.has ("type") ? defaultProfile : Profile().toJson();
    TranscriptionSettings fresh;
    fresh.transpose = defaultTranspose;
    v.settings = fresh.toJson();
    v.regenerate();

    list.push_back (std::move (v));
    activeVersionId = list.back().id;
    ++structureRevision;
    return activeVersionId;
}

bool Document::select (const std::string& id)
{
    if (find (id) == nullptr)
        return false;

    if (activeVersionId != id)
    {
        activeVersionId = id;
        ++structureRevision;
    }

    return true;
}

bool Document::rename (const std::string& id, const std::string& newName)
{
    auto* v = findMutable (id);

    if (v == nullptr || newName.empty())
        return false;

    v->name = newName;
    ++structureRevision;
    return true;
}

namespace
{
    // One line of at most 120 characters (counted in characters, not bytes), no control characters, trimmed.
    std::string oneLine (const std::string& text)
    {
        std::string out;
        size_t characters = 0;

        for (size_t i = 0; i < text.size() && characters < 120;)
        {
            const auto c = (unsigned char) text[i];
            const size_t length = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;

            if (c < 0x20 || c == 0x7f || i + length > text.size())
            {
                out += c < 0x20 || c == 0x7f ? ' ' : '?';
                ++i;
            }
            else
            {
                out.append (text, i, length);
                i += length;
            }

            ++characters;
        }

        const auto first = out.find_first_not_of (' ');
        const auto last = out.find_last_not_of (' ');
        return first == std::string::npos ? std::string() : out.substr (first, last - first + 1);
    }
}

bool Document::setMeta (const std::string& id, const std::string& title, const std::string& composer)
{
    auto* v = findMutable (id);

    if (v == nullptr)
        return false;

    const auto t = oneLine (title), c = oneLine (composer);

    if (t == v->title && c == v->composer)
        return true;

    v->title = t;
    v->composer = c;
    ++v->metaRevision;
    ++structureRevision;
    return true;
}

std::string Document::duplicate (const std::string& id, int64_t nowMs)
{
    const auto* source = find (id);

    if (source == nullptr)
        return {};

    Version copy = *source;
    copy.id = makeId();
    copy.name = source->name + " copy";
    copy.createdAtMs = nowMs;

    // The copy goes right after its source.
    const auto at = (size_t) (source - list.data()) + 1;
    list.insert (list.begin() + (std::ptrdiff_t) at, std::move (copy));
    activeVersionId = list[at].id;
    ++structureRevision;
    return activeVersionId;
}

bool Document::remove (const std::string& id)
{
    const auto* v = find (id);

    if (v == nullptr)
        return false;

    const auto at = (size_t) (v - list.data());
    list.erase (list.begin() + (std::ptrdiff_t) at);

    if (activeVersionId == id)
        activeVersionId = list.empty() ? std::string() : list[at > 0 ? at - 1 : 0].id;

    ++structureRevision;
    return true;
}

uint64_t Document::revision() const
{
    auto r = structureRevision;

    for (const auto& v : list)
        r += v.revision();

    return r;
}

//==============================================================================
Json Document::toJson() const
{
    auto j = Json::object();
    j.set ("schemaVersion", currentSchema);
    j.set ("activeVersionId", activeVersionId);
    j.set ("nextTake", nextTake);
    j.set ("nextVersionId", idCounter);
    j.set ("uiPrefs", uiPrefs);
    j.set ("drumMaps", drumMaps);
    j.set ("defaultProfile", defaultProfile);

    if (defaultTranspose != 0)
        j.set ("defaultTranspose", defaultTranspose);

    auto versions = Json::array();

    for (const auto& v : list)
    {
        auto o = Json::object();
        o.set ("id", v.id);
        o.set ("name", v.name);

        if (! v.title.empty())
            o.set ("title", v.title);

        if (! v.composer.empty())
            o.set ("composer", v.composer);

        o.set ("createdAt", v.createdAtMs);
        o.set ("profile", v.profile);
        o.set ("settings", v.settings);
        o.set ("capture", captureToJson (v.capture));
        o.set ("reading", readingToJson (v.reading));
        o.set ("detection", detectionToJson (v.detection));
        // A score nobody has edited is made again from the recording when the state is loaded, which
        // keeps the saved state small and lets a better transcription improve old takes.
        if (v.scoreEdited)
            o.set ("score", v.score.toJson());

        o.set ("scoreEdited", v.scoreEdited);
        o.set ("transcription", transcriptionToJson (v.key, v.report));
        versions.push (std::move (o));
    }

    j.set ("versions", std::move (versions));
    return j;
}

LoadResult Document::fromJson (const Json& source, Document& result, std::string* error, int targetSchema,
                               const std::vector<Migration>& steps)
{
    Json j = source;

    if (j.isObject() && j.get ("schemaVersion").isNumber() && j.get ("schemaVersion").asInt() > targetSchema)
    {
        failWith (error, "the saved state was written by a newer version of Transcriber");
        return LoadResult::tooNew;
    }

    if (! migrateDocument (j, targetSchema, steps, error))
        return LoadResult::invalid;

    Document d;
    d.nextTake = std::max (1, (int) j.get ("nextTake").asInt (1));
    d.idCounter = std::max<int64_t> (1, j.get ("nextVersionId").asInt (1));
    d.uiPrefs = j.get ("uiPrefs").isObject() ? j.get ("uiPrefs") : Json::object();
    d.drumMaps = j.get ("drumMaps").isArray() ? j.get ("drumMaps") : Json::array();
    d.defaultProfile = j.get ("defaultProfile").isObject() ? j.get ("defaultProfile") : Json::object();
    d.defaultTranspose = std::max (-48, std::min (48, (int) j.get ("defaultTranspose").asInt (0)));

    std::set<std::string> ids;

    for (const auto& o : j.get ("versions").items())
    {
        Version v;
        v.id = o.get ("id").asString();
        v.name = o.get ("name").asString();
        v.title = oneLine (o.get ("title").asString());
        v.composer = oneLine (o.get ("composer").asString());
        v.createdAtMs = o.get ("createdAt").asInt();
        v.profile = o.get ("profile").isObject() ? o.get ("profile") : Json::object();
        v.settings = o.get ("settings").isObject() ? o.get ("settings") : Json::object();

        if (v.id.empty() || ! ids.insert (v.id).second)
        {
            failWith (error, "a saved version has a missing or repeated id");
            return LoadResult::invalid;
        }

        if (! captureFromJson (o.get ("capture"), v.capture, error))
            return LoadResult::invalid;

        v.reading = readingFromJson (o.get ("reading"));
        v.detection = detectionFromJson (o.get ("detection"));

        if (o.has ("score") && ! Score::fromJson (o.get ("score"), v.score, error))
            return LoadResult::invalid;

        v.scoreEdited = o.get ("scoreEdited").asBool();
        transcriptionFromJson (o.get ("transcription"), v.key, v.report);

        // Not edited (or saved by a build that did not make scores yet): make the score from the recording.
        if (! v.scoreEdited || v.score.root().children.empty())
        {
            v.scoreEdited = false;
            v.regenerate();
        }

        d.list.push_back (std::move (v));
    }

    const auto wanted = j.get ("activeVersionId").asString();
    d.activeVersionId = d.find (wanted) != nullptr ? wanted : (d.list.empty() ? std::string() : d.list.back().id);

    result = std::move (d);
    return LoadResult::ok;
}

}  // namespace trs
