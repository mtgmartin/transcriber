#include "Instruments.h"

#include <algorithm>
#include <cctype>

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
// Tunings (Phase 8f)

std::vector<TuningPreset> tuningPresets (InstrumentType t)
{
    if (t == InstrumentType::guitar)
        return { { "std", "Standard", { 40, 45, 50, 55, 59, 64 } },
                 { "dropd", "Drop D", { 38, 45, 50, 55, 59, 64 } },
                 { "dropdd", "Double drop D", { 38, 45, 50, 55, 59, 62 } },
                 { "dadgad", "DADGAD", { 38, 45, 50, 55, 57, 62 } },
                 { "openg", "Open G", { 38, 43, 50, 55, 59, 62 } },
                 { "opend", "Open D", { 38, 45, 50, 54, 57, 62 } },
                 { "opene", "Open E", { 40, 47, 52, 56, 59, 64 } },
                 { "half", "Half step down", { 39, 44, 49, 54, 58, 63 } },
                 { "whole", "Whole step down (D standard)", { 38, 43, 48, 53, 57, 62 } },
                 { "dropc", "Drop C", { 36, 43, 48, 53, 57, 62 } },
                 { "cstd", "C standard", { 36, 41, 46, 51, 55, 60 } },
                 { "g7", "7 strings (B standard)", { 35, 40, 45, 50, 55, 59, 64 } },
                 { "g8", "8 strings (F# standard)", { 30, 35, 40, 45, 50, 55, 59, 64 } } };

    if (t == InstrumentType::bass)
        return { { "std", "Standard", { 28, 33, 38, 43 } },
                 { "dropd", "Drop D", { 26, 33, 38, 43 } },
                 { "half", "Half step down", { 27, 32, 37, 42 } },
                 { "whole", "Whole step down (D standard)", { 26, 31, 36, 41 } },
                 { "b5", "5 strings (B E A D G)", { 23, 28, 33, 38, 43 } },
                 { "b6", "6 strings (B E A D G C)", { 23, 28, 33, 38, 43, 48 } } };

    return {};
}

std::string noteNameOf (int midi)
{
    static const char* const names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const auto pc = ((midi % 12) + 12) % 12;
    const auto octave = (midi - pc) / 12 - 1;
    return std::string (names[pc]) + std::to_string (octave);
}

std::string tuningText (const std::vector<int>& notes)
{
    std::string text;

    for (const auto n : notes)
        text += (text.empty() ? "" : " ") + std::to_string (n);

    return text;
}

std::vector<int> tuningFromText (const std::string& text)
{
    std::vector<int> out;
    size_t at = 0;

    while (at < text.size())
    {
        while (at < text.size() && text[at] == ' ')
            ++at;

        if (at >= text.size())
            break;

        size_t end = at;

        while (end < text.size() && text[end] >= '0' && text[end] <= '9')
            ++end;

        if (end == at || (end < text.size() && text[end] != ' ') || end - at > 3)
            return {};

        const int value = std::stoi (text.substr (at, end - at));

        if (value > 127)
            return {};

        out.push_back (value);
        at = end;
    }

    return out;
}

bool tuningFromNames (const std::string& text, std::vector<int>& out, std::string& error)
{
    out.clear();
    std::vector<std::string> words;
    std::string word;

    for (const auto c : text)
    {
        if (c == ' ' || c == ',' || c == ';' || c == '\t')
        {
            if (! word.empty())
                words.push_back (word);

            word.clear();
        }
        else
        {
            word += c;
        }
    }

    if (! word.empty())
        words.push_back (word);

    if (words.empty())
    {
        error = "Type the notes of the strings from the lowest, for example D2 A2 D3 G3 B3 E4 (or MIDI numbers).";
        return false;
    }

    static const int pcs[7] = { 9, 11, 0, 2, 4, 5, 7 };   // A B C D E F G

    for (const auto& w : words)
    {
        const bool digits = std::all_of (w.begin(), w.end(), [] (char c) { return c >= '0' && c <= '9'; });

        if (digits)
        {
            if (w.size() > 3 || std::stoi (w) > 127)
            {
                error = "\"" + w + "\" is not a MIDI note (0 to 127).";
                return false;
            }

            out.push_back (std::stoi (w));
            continue;
        }

        const auto letter = (char) std::toupper ((unsigned char) w[0]);

        if (letter < 'A' || letter > 'G')
        {
            error = "\"" + w + "\" is not a note: write a letter A to G, an optional # or b, and the octave, for example F#2.";
            return false;
        }

        size_t i = 1;
        int alter = 0;

        while (i < w.size() && (w[i] == '#' || w[i] == 'b' || w[i] == 'B'))
        {
            alter += w[i] == '#' ? 1 : -1;
            ++i;
        }

        const bool negative = i < w.size() && w[i] == '-';

        if (negative)
            ++i;

        if (i >= w.size() || ! std::all_of (w.begin() + (std::ptrdiff_t) i, w.end(), [] (char c) { return c >= '0' && c <= '9'; }) || w.size() - i > 1)
        {
            error = "\"" + w + "\" has no octave: write for example " + std::string (1, letter) + "2.";
            return false;
        }

        const int octave = (negative ? -1 : 1) * (w[i] - '0');
        const int midi = 12 * (octave + 1) + pcs[letter - 'A'] + alter;

        if (midi < 0 || midi > 127)
        {
            error = "\"" + w + "\" is outside the MIDI notes 0 to 127.";
            return false;
        }

        out.push_back (midi);
    }

    return true;
}

bool validTuning (InstrumentType t, const std::vector<int>& notes, std::string* why)
{
    auto no = [&] (const std::string& message)
    {
        if (why != nullptr)
            *why = message;

        return false;
    };

    if (t != InstrumentType::guitar && t != InstrumentType::bass)
        return no ("Only a guitar or a bass has strings.");

    const size_t most = t == InstrumentType::guitar ? 8 : 6;

    if (notes.size() < 4 || notes.size() > most)
        return no ((t == InstrumentType::guitar ? "A guitar has 4 to 8 strings" : "A bass has 4 to 6 strings") + std::string (", not ") + std::to_string (notes.size()) + ".");

    for (size_t i = 0; i < notes.size(); ++i)
    {
        if (notes[i] < 0 || notes[i] > 127)
            return no ("The notes must be MIDI notes from 0 to 127.");

        if (i > 0 && notes[i] <= notes[i - 1])
            return no ("The strings must go up from the lowest: " + noteNameOf (notes[i]) + " is not above " + noteNameOf (notes[i - 1]) + ".");
    }

    return true;
}

std::string tuningName (InstrumentType t, const std::vector<int>& notes)
{
    for (const auto& p : tuningPresets (t))
        if (p.notes == notes)
            return p.name;

    return tuningLetters (notes);
}

std::string tuningLetters (const std::vector<int>& notes)
{
    std::string letters;

    for (const auto n : notes)
    {
        auto name = noteNameOf (n);
        name.erase (std::find_if (name.begin(), name.end(), [] (char c) { return (c >= '0' && c <= '9') || c == '-'; }), name.end());
        letters += (letters.empty() ? "" : " ") + name;
    }

    return letters;
}

std::vector<int> chooseTuning (InstrumentType t, const std::vector<int>& pitches, int maxFret)
{
    const auto standard = openStrings (t);

    if (standard.empty())
        return standard;

    auto missed = [&] (const std::vector<int>& tuning)
    {
        int n = 0;

        for (const auto p : pitches)
            if (p < tuning.front() || p > tuning.back() + maxFret)
                ++n;

        return n;
    };

    if (missed (standard) == 0)
        return standard;

    // the candidates, the ones a player would think of first at the top: a drop tuning, an extra low string, everything tuned
    // down, a drop tuning of that, any other lower string, an extra low string and everything tuned down
    std::vector<std::vector<int>> candidates;
    const auto down = [&] (int semitones) { auto v = standard; for (auto& n : v) n -= semitones; return v; };
    const auto lowered = [&] (std::vector<int> v, int semitones) { v.front() -= semitones; return v; };
    const auto withLow = [&] (std::vector<int> v) { v.insert (v.begin(), v.front() - 5); return v; };
    const auto limit = t == InstrumentType::guitar ? (size_t) 8 : (size_t) 6;
    const auto room = [&] (const std::vector<int>& v) { return v.size() <= limit && v.front() >= 0 && v.back() + maxFret <= 127; };

    auto add = [&] (const std::vector<int>& v)
    {
        if (room (v) && std::find (candidates.begin(), candidates.end(), v) == candidates.end())
            candidates.push_back (v);
    };

    add (lowered (standard, 2));
    add (withLow (standard));

    for (int s = 1; s <= 4; ++s)
        add (down (s));

    for (int s = 1; s <= 4; ++s)
        add (lowered (down (s), 2));

    for (int d = 1; d <= 7; ++d)
        add (lowered (standard, d));

    for (int s = 1; s <= 4; ++s)
        add (withLow (down (s)));

    add (withLow (withLow (standard)));

    // a bass with six strings also has a high C
    if (t == InstrumentType::bass)
    {
        auto six = withLow (standard);
        six.push_back (standard.back() + 5);
        add (six);
    }

    int bestMissed = missed (standard);
    std::vector<int> best = standard;

    for (const auto& c : candidates)
    {
        const auto m = missed (c);

        if (m == 0)
            return c;

        if (m < bestMissed)
        {
            bestMissed = m;
            best = c;
        }
    }

    return best;
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
