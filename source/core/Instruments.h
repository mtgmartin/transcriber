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

// Tunings of a guitar or bass (Phase 8f): the open strings as MIDI notes, lowest string first, strictly rising.
struct TuningPreset
{
    std::string id;      // "std", "dropd", ...
    std::string name;    // "Drop D"
    std::vector<int> notes;
};

std::vector<TuningPreset> tuningPresets (InstrumentType);   // the first is the standard tuning; empty for other instruments
std::string noteNameOf (int midi);                          // "E2", "C#3" (middle C is C4)
std::string tuningText (const std::vector<int>&);           // "40 45 50 55 59 64": how a tuning is saved
std::vector<int> tuningFromText (const std::string&);       // the numbers; empty if that is not a list of numbers 0-127
// A tuning typed by the user: note names ("D2 A2 D3", "C#2 Db3") and/or MIDI numbers, separated by spaces or commas.
bool tuningFromNames (const std::string& text, std::vector<int>& out, std::string& error);
// Whether the notes can be the strings of this instrument: 4-8 strings for a guitar, 4-6 for a bass, rising, within MIDI 0-127.
bool validTuning (InstrumentType, const std::vector<int>&, std::string* why = nullptr);
// "Standard", "Drop D", ... or the letters of the strings ("D A D G B E").
std::string tuningName (InstrumentType, const std::vector<int>&);
std::string tuningLetters (const std::vector<int>&);        // always the letters of the strings: "D A D G B E"
// The tuning a take is written in when the user left it to the program: the standard one if it can play every note, otherwise the
// smallest change (a lower string, everything tuned down, an extra low string) that can; the best one if none can.
std::vector<int> chooseTuning (InstrumentType, const std::vector<int>& pitches, int maxFret);

}  // namespace trs
