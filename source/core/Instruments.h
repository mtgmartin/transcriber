#pragma once

#include "Json.h"

#include <string>
#include <vector>

// The instrument a take is written for, and the drum map that says how MIDI notes become drum notation.
namespace trs
{

enum class InstrumentType { piano, drums, guitar, bass };

const char* instrumentName (InstrumentType);                        // "piano", "drums", "guitar", "bass"
InstrumentType instrumentFromName (const std::string&);             // anything else gives piano

// One line of a drum map: which MIDI note is which drum, and where and how it is written.
struct DrumEntry
{
    int note = 36;
    std::string name = "Kick";
    int loc = 1;                // staff position as in MEI @loc: 0 = bottom line, 1 = first space, ... 8 = top line, 9 = space above, 10 = first ledger line above, -1 = space below
    std::string head = "normal";   // "normal", "x", "open-x" (open hi-hat), "diamond"
    int voice = 2;              // 1 = hands (stems up), 2 = feet (stems down)
    int ghostBelow = 0;         // hits softer than this velocity are written as ghost notes in brackets; 0 = never

    Json toJson() const;
    static DrumEntry fromJson (const Json&);
    bool operator== (const DrumEntry&) const;
};

struct DrumMap
{
    std::string id = "gm";
    std::string name = "General MIDI";
    std::vector<DrumEntry> entries;

    const DrumEntry* find (int note) const;
    // Adds the entry, or replaces the one with the same note.
    void set (const DrumEntry&);
    bool remove (int note);

    Json toJson() const;
    // false (with a message) when the JSON is not a drum map. Entries with notes outside 0-127 are refused.
    static bool fromJson (const Json&, DrumMap& result, std::string* error = nullptr);
    bool operator== (const DrumMap&) const;
};

// The built-in maps: "gm" (the usual General MIDI kit) and "gm2" (the same plus the extra percussion of GM 2).
// `makeEmptyDrumMap` is a map with no entries to start a map of your own from.
std::vector<DrumMap> drumPresets();
DrumMap drumPreset (const std::string& id);          // "gm" if the id is not a preset
DrumMap makeEmptyDrumMap (const std::string& id, const std::string& name);

// What a version keeps about the instrument: its type and, for drums, a copy of the map used.
struct Profile
{
    InstrumentType type = InstrumentType::piano;
    DrumMap drumMap = drumPreset ("gm");

    Json toJson() const;
    static Profile fromJson (const Json&);
    bool operator== (const Profile&) const;
};

// Standard tunings: MIDI notes of the open strings, lowest string first.
std::vector<int> openStrings (InstrumentType);       // guitar E2 A2 D3 G3 B3 E4; bass E1 A1 D2 G2; empty for others

}  // namespace trs
