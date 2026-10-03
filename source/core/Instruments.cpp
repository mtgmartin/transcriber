#include "Instruments.h"

#include <algorithm>

namespace trs
{

const char* instrumentName (InstrumentType t)
{
    switch (t)
    {
        case InstrumentType::drums:  return "drums";
        case InstrumentType::guitar: return "guitar";
        case InstrumentType::bass:   return "bass";
        default:                     return "piano";
    }
}

InstrumentType instrumentFromName (const std::string& name)
{
    if (name == "drums")  return InstrumentType::drums;
    if (name == "guitar") return InstrumentType::guitar;
    if (name == "bass")   return InstrumentType::bass;
    return InstrumentType::piano;
}

std::vector<int> openStrings (InstrumentType t)
{
    if (t == InstrumentType::guitar) return { 40, 45, 50, 55, 59, 64 };
    if (t == InstrumentType::bass)   return { 28, 33, 38, 43 };
    return {};
}

//==============================================================================
Json DrumEntry::toJson() const
{
    auto j = Json::object();
    j.set ("note", note);
    j.set ("name", name);
    j.set ("loc", loc);
    j.set ("head", head);
    j.set ("voice", voice);
    j.set ("ghostBelow", ghostBelow);
    return j;
}

DrumEntry DrumEntry::fromJson (const Json& j)
{
    DrumEntry e;
    e.note = std::max (0, std::min (127, (int) j.get ("note").asInt (e.note)));
    e.name = j.get ("name").asString (e.name).substr (0, 60);
    e.loc = std::max (-6, std::min (16, (int) j.get ("loc").asInt (e.loc)));

    const auto head = j.get ("head").asString (e.head);
    e.head = head == "x" || head == "open-x" || head == "diamond" ? head : "normal";

    e.voice = j.get ("voice").asInt (e.voice) == 1 ? 1 : 2;
    e.ghostBelow = std::max (0, std::min (127, (int) j.get ("ghostBelow").asInt (0)));
    return e;
}

bool DrumEntry::operator== (const DrumEntry& o) const
{
    return note == o.note && name == o.name && loc == o.loc && head == o.head && voice == o.voice && ghostBelow == o.ghostBelow;
}

//==============================================================================
const DrumEntry* DrumMap::find (int note) const
{
    for (const auto& e : entries)
        if (e.note == note)
            return &e;

    return nullptr;
}

void DrumMap::set (const DrumEntry& entry)
{
    for (auto& e : entries)
    {
        if (e.note == entry.note)
        {
            e = entry;
            return;
        }
    }

    entries.push_back (entry);
    std::sort (entries.begin(), entries.end(), [] (const DrumEntry& a, const DrumEntry& b) { return a.note < b.note; });
}

bool DrumMap::remove (int note)
{
    const auto it = std::remove_if (entries.begin(), entries.end(), [note] (const DrumEntry& e) { return e.note == note; });

    if (it == entries.end())
        return false;

    entries.erase (it, entries.end());
    return true;
}

Json DrumMap::toJson() const
{
    auto j = Json::object();
    j.set ("id", id);
    j.set ("name", name);
    auto list = Json::array();

    for (const auto& e : entries)
        list.push (e.toJson());

    j.set ("entries", std::move (list));
    return j;
}

bool DrumMap::fromJson (const Json& j, DrumMap& result, std::string* error)
{
    auto fail = [&] (const std::string& message)
    {
        if (error != nullptr)
            *error = message;

        return false;
    };

    if (! j.isObject() || ! j.get ("entries").isArray())
        return fail ("this is not a drum map (it has no list of entries)");

    DrumMap map;
    map.id = j.get ("id").asString ("custom").substr (0, 40);
    map.name = j.get ("name").asString ("Drum map").substr (0, 60);

    if (map.id.empty())
        map.id = "custom";

    if (map.name.empty())
        map.name = "Drum map";

    for (const auto& item : j.get ("entries").items())
    {
        if (! item.isObject() || ! item.get ("note").isNumber())
            return fail ("an entry of the drum map has no note number");

        const auto note = item.get ("note").asInt();

        if (note < 0 || note > 127)
            return fail ("a note number in the drum map is outside 0-127");

        map.set (DrumEntry::fromJson (item));
    }

    result = std::move (map);
    return true;
}

bool DrumMap::operator== (const DrumMap& o) const
{
    return id == o.id && name == o.name && entries == o.entries;
}

//==============================================================================
namespace
{
    DrumEntry e (int note, const char* name, int loc, const char* head, int voice, int ghost = 0)
    {
        DrumEntry d;
        d.note = note;
        d.name = name;
        d.loc = loc;
        d.head = head;
        d.voice = voice;
        d.ghostBelow = ghost;
        return d;
    }

    DrumMap generalMidi()
    {
        DrumMap m;
        m.id = "gm";
        m.name = "General MIDI";
        m.entries = {
            e (35, "Bass drum 2", 1, "normal", 2),
            e (36, "Bass drum 1", 1, "normal", 2),
            e (37, "Side stick", 5, "x", 1, 45),
            e (38, "Snare", 5, "normal", 1, 45),
            e (39, "Hand clap", 6, "x", 1),
            e (40, "Electric snare", 5, "normal", 1, 45),
            e (41, "Low floor tom", 0, "normal", 1),
            e (42, "Closed hi-hat", 9, "x", 1),
            e (43, "High floor tom", 2, "normal", 1),
            e (44, "Hi-hat pedal", -1, "x", 2),
            e (45, "Low tom", 3, "normal", 1),
            e (46, "Open hi-hat", 9, "open-x", 1),
            e (47, "Low-mid tom", 4, "normal", 1),
            e (48, "Hi-mid tom", 6, "normal", 1),
            e (49, "Crash cymbal 1", 10, "x", 1),
            e (50, "High tom", 7, "normal", 1),
            e (51, "Ride cymbal 1", 8, "x", 1),
            e (52, "Chinese cymbal", 10, "x", 1),
            e (53, "Ride bell", 8, "diamond", 1),
            e (54, "Tambourine", 9, "diamond", 1),
            e (55, "Splash cymbal", 10, "x", 1),
            e (56, "Cowbell", 7, "diamond", 1),
            e (57, "Crash cymbal 2", 10, "x", 1),
            e (59, "Ride cymbal 2", 8, "x", 1)
        };
        return m;
    }
}

std::vector<DrumMap> drumPresets()
{
    auto gm = generalMidi();

    auto gm2 = gm;
    gm2.id = "gm2";
    gm2.name = "General MIDI 2";
    const DrumEntry extra[] = {
        e (27, "High Q", 9, "diamond", 1),
        e (28, "Slap", 9, "diamond", 1),
        e (29, "Scratch push", 9, "diamond", 1),
        e (30, "Scratch pull", 9, "diamond", 1),
        e (31, "Sticks", 9, "diamond", 1),
        e (32, "Square click", 9, "diamond", 1),
        e (33, "Metronome click", 9, "diamond", 1),
        e (34, "Metronome bell", 9, "diamond", 1),
        e (58, "Vibraslap", 7, "x", 1),
        e (60, "Hi bongo", 10, "normal", 1),
        e (61, "Low bongo", 9, "normal", 1),
        e (62, "Mute hi conga", 10, "x", 1),
        e (63, "Open hi conga", 10, "normal", 1),
        e (64, "Low conga", 9, "normal", 1),
        e (65, "High timbale", 8, "normal", 1),
        e (66, "Low timbale", 7, "normal", 1),
        e (67, "High agogo", 10, "diamond", 1),
        e (68, "Low agogo", 9, "diamond", 1),
        e (69, "Cabasa", 9, "x", 1),
        e (70, "Maracas", 9, "x", 1),
        e (71, "Short whistle", 10, "diamond", 1),
        e (72, "Long whistle", 10, "diamond", 1),
        e (73, "Short guiro", 9, "x", 1),
        e (74, "Long guiro", 9, "x", 1),
        e (75, "Claves", 9, "x", 1),
        e (76, "Hi wood block", 10, "normal", 1),
        e (77, "Low wood block", 9, "normal", 1),
        e (78, "Mute cuica", 10, "x", 1),
        e (79, "Open cuica", 10, "normal", 1),
        e (80, "Mute triangle", 10, "diamond", 1),
        e (81, "Open triangle", 10, "diamond", 1)
    };

    for (const auto& x : extra)
        gm2.set (x);

    return { gm, gm2 };
}

DrumMap drumPreset (const std::string& id)
{
    for (auto& m : drumPresets())
        if (m.id == id)
            return m;

    return generalMidi();
}

DrumMap makeEmptyDrumMap (const std::string& id, const std::string& name)
{
    DrumMap m;
    m.id = id;
    m.name = name;
    return m;
}

//==============================================================================
Json Profile::toJson() const
{
    auto j = Json::object();
    j.set ("type", instrumentName (type));

    if (type == InstrumentType::drums)
        j.set ("drumMap", drumMap.toJson());

    return j;
}

Profile Profile::fromJson (const Json& j)
{
    Profile p;
    p.type = instrumentFromName (j.get ("type").asString());

    if (j.has ("drumMap"))
    {
        DrumMap map;

        if (DrumMap::fromJson (j.get ("drumMap"), map))
            p.drumMap = std::move (map);
    }

    return p;
}

bool Profile::operator== (const Profile& o) const
{
    return type == o.type && (type != InstrumentType::drums || drumMap == o.drumMap);
}

}  // namespace trs
