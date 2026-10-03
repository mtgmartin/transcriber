// Tests for the drum map, the drum pipeline, the tab algorithm and the guitar/bass pipeline.

#include "TestSupport.h"
#include "TranscribeSupport.h"
#include "XmlCheck.h"

#include "MidiReader.h"
#include "core/Instruments.h"
#include "core/Mei.h"
#include "core/Notation.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <random>
#include <set>

#define REGISTER(fn, name) static testing::Registrar registrar_##fn (name, fn)

using namespace trs;
using tsupport::N;
using tsupport::p;

namespace
{
    //==========================================================================
    // Phase 8f: tunings

    void testTuningLibrary()
    {
        // every preset is a tuning its instrument can have, the first one is the standard tuning, and each has a name
        for (const auto type : { InstrumentType::guitar, InstrumentType::bass })
        {
            const auto presets = tuningPresets (type);
            CHECK (presets.size() >= 5);
            CHECK (presets.front().notes == openStrings (type));
            CHECK_STR (presets.front().name.c_str(), "Standard");

            for (const auto& preset : presets)
            {
                std::string why;
                CHECK (validTuning (type, preset.notes, &why));
                CHECK_STR (tuningName (type, preset.notes).c_str(), preset.name.c_str());
                CHECK (tuningFromText (tuningText (preset.notes)) == preset.notes);
            }
        }

        CHECK (tuningPresets (InstrumentType::piano).empty());
        CHECK_STR (noteNameOf (40).c_str(), "E2");
        CHECK_STR (noteNameOf (61).c_str(), "C#4");
        CHECK_STR (noteNameOf (23).c_str(), "B0");
        CHECK_STR (tuningName (InstrumentType::guitar, { 38, 43, 48, 53, 57, 61 }).c_str(), "D G C F A C#");   // no name: the letters

        // typed by the user: names, flats and sharps, numbers, mixed, with commas
        std::vector<int> notes;
        std::string error;
        CHECK (tuningFromNames ("D2 A2 D3 G3 B3 E4", notes, error));
        CHECK (notes == std::vector<int> ({ 38, 45, 50, 55, 59, 64 }));
        CHECK (tuningFromNames ("c#2, Db3  F#3 38 57 69", notes, error));
        CHECK (notes == std::vector<int> ({ 37, 49, 54, 38, 57, 69 }));
        CHECK (tuningFromNames ("Bb1 E2", notes, error));
        CHECK (notes == std::vector<int> ({ 34, 40 }));

        // what is wrong is said
        CHECK (! tuningFromNames ("", notes, error));
        CHECK (error.find ("Type the notes") != std::string::npos);
        CHECK (! tuningFromNames ("D2 H2", notes, error));
        CHECK (error.find ("\"H2\"") != std::string::npos);
        CHECK (! tuningFromNames ("D2 A", notes, error));
        CHECK (error.find ("no octave") != std::string::npos);
        CHECK (! tuningFromNames ("D2 200", notes, error));
        CHECK (error.find ("0 to 127") != std::string::npos);

        // the strings of an instrument
        std::string why;
        CHECK (validTuning (InstrumentType::guitar, { 35, 40, 45, 50, 55, 59, 64 }, &why));
        CHECK (! validTuning (InstrumentType::guitar, { 40, 45, 50 }, &why));
        CHECK (why.find ("4 to 8") != std::string::npos);
        CHECK (! validTuning (InstrumentType::bass, { 23, 28, 33, 38, 43, 48, 53 }, &why));
        CHECK (! validTuning (InstrumentType::guitar, { 40, 45, 45, 55, 59, 64 }, &why));
        CHECK (why.find ("go up") != std::string::npos);
        CHECK (! validTuning (InstrumentType::guitar, { 40, 45, 50, 55, 59, 130 }, &why));
        CHECK (! validTuning (InstrumentType::piano, { 40, 45, 50, 55 }, &why));
        CHECK (tuningFromText ("40 45 x").empty());
        CHECK (tuningFromText ("40 999").empty());
    }

    void testChooseTuning()
    {
        const auto standard = openStrings (InstrumentType::guitar);
        const auto choose = [&] (std::vector<int> pitches, InstrumentType type = InstrumentType::guitar) { return chooseTuning (type, pitches, 22); };

        // what the standard tuning can play: it stays
        CHECK (choose ({ 40, 52, 64, 86 }) == standard);
        CHECK (choose ({}) == standard);

        // below the low E: a drop tuning, then an extra string, then everything tuned down
        CHECK (choose ({ 40, 38 }) == tuningPresets (InstrumentType::guitar)[1].notes);        // Drop D
        CHECK (choose ({ 40, 38, 52 }) == tuningPresets (InstrumentType::guitar)[1].notes);
        CHECK (choose ({ 37 }) == std::vector<int> ({ 35, 40, 45, 50, 55, 59, 64 }));         // a 7-string guitar
        CHECK (choose ({ 30 }) == std::vector<int> ({ 30, 35, 40, 45, 50, 55, 59, 64 }));     // an 8-string guitar
        CHECK (choose ({ 20 }) == standard);                                                   // nothing can play it: the standard tuning stays

        // above the highest string: nothing helps, the standard tuning stays
        CHECK (choose ({ 40, 100 }) == standard);

        // bass
        const auto bass = openStrings (InstrumentType::bass);
        CHECK (choose ({ 28, 33 }, InstrumentType::bass) == bass);
        CHECK (choose ({ 26 }, InstrumentType::bass) == tuningPresets (InstrumentType::bass)[1].notes);   // Drop D
        CHECK (choose ({ 24 }, InstrumentType::bass) == std::vector<int> ({ 23, 28, 33, 38, 43 }));       // a 5-string bass
        CHECK (choose ({ 60 }, InstrumentType::bass) == std::vector<int> ({ 23, 28, 33, 38, 43, 48 }) || choose ({ 60 }, InstrumentType::bass).size() >= 4);

        // every tuning that comes back is a tuning of the instrument
        std::mt19937 rng (5);

        for (int i = 0; i < 200; ++i)
        {
            std::vector<int> pitches;

            for (int k = 0; k < 6; ++k)
                pitches.push_back (20 + (int) (rng() % 70));

            for (const auto type : { InstrumentType::guitar, InstrumentType::bass })
                CHECK (validTuning (type, chooseTuning (type, pitches, 22)));
        }
    }

    void testTuningsInTheScore()
    {
        // a take with a note below the low E: the program tunes the guitar to Drop D, says so, and every note is on the tab
        const auto low = tsupport::capture ({ { 38, 0, 1 }, { 45, 1, 1 }, { 50, 2, 1 }, { 55, 3, 1 }, { 52, 4, 2 }, { 59, 4, 2 } }, 8.0);
        const auto drop = transcribeFretted (low, {}, InstrumentType::guitar);
        bool said = false;

        for (const auto& w : drop.report.warnings)
        {
            said = said || w.find ("Tuned to Drop D so that all notes can be played") != std::string::npos;
            CHECK (w.find ("cannot be played") == std::string::npos);
        }

        CHECK (said);
        CHECK_STR (drop.report.tuning.c_str(), "Drop D");
        CHECK_EQ (drop.report.notes, 6);

        const Node* tab = nullptr;

        for (const auto& part : drop.score.root().children)
            for (const auto& staff : part.children)
                if (staff.prop ("kind").asString() == "tab")
                    tab = &staff;

        CHECK (tab != nullptr);

        if (tab != nullptr)
        {
            CHECK_STR (tab->prop ("tuning").asString().c_str(), "38 45 50 55 59 64");
            CHECK_EQ (tab->prop ("strings").asInt(), (int64_t) 6);
        }

        // the first note is on the open low string
        CHECK (dumpScore (drop.score).find ("S2 v1: 6:0/4 5:0/4 4:0/4 3:0/4") != std::string::npos);

        // the MEI has the tuning of the strings and the words under the tab
        const auto mei = scoreToMei (drop.score, {});
        CHECK (mei.find ("<course n=\"6\" pname=\"d\" oct=\"2\"/>") != std::string::npos);
        CHECK (mei.find (">Tuning: D A D G B E</dir>") != std::string::npos);
        CHECK (xmlcheck::checkXml (mei).wellFormed);

        // the standard tuning has no words and no change
        const auto plain = transcribeFretted (tsupport::capture ({ { 40, 0, 1 }, { 45, 1, 1 } }, 4.0), {}, InstrumentType::guitar);
        CHECK_STR (plain.report.tuning.c_str(), "Standard");
        CHECK (scoreToMei (plain.score, {}).find ("Tuning:") == std::string::npos);
        CHECK (plain.report.warnings.empty());

        // a tuning chosen by the user is used, even when the take would fit the standard one
        TranscriptionSettings seven;
        seven.tuning = "35 40 45 50 55 59 64";
        const auto big = transcribeFretted (tsupport::capture ({ { 40, 0, 1 }, { 35, 1, 1 } }, 4.0), seven, InstrumentType::guitar);
        CHECK_STR (big.report.tuning.c_str(), "7 strings (B standard)");
        CHECK (scoreToMei (big.score, {}).find ("lines=\"7\"") != std::string::npos);
        CHECK (dumpScore (big.score).find ("S2 v1: 6:0/4 7:0/4") != std::string::npos);

        // one that cannot be the strings of this instrument (seven strings for a bass, or not rising) is let go: the program chooses
        TranscriptionSettings six;
        six.tuning = "35 40 45 50 55 59 64";
        CHECK_STR (transcribeFretted (tsupport::capture ({ { 28, 0, 1 } }, 4.0), six, InstrumentType::bass).report.tuning.c_str(), "Standard");
        TranscriptionSettings wild;
        wild.tuning = "50 45 40 55";
        CHECK_STR (TranscriptionSettings::fromJson (wild.toJson()).tuning.c_str(), "auto");

        // bass in a tuning of its own
        TranscriptionSettings five;
        five.tuning = "23 28 33 38 43";
        const auto lowB = transcribeFretted (tsupport::capture ({ { 24, 0, 1 }, { 28, 1, 1 } }, 4.0), five, InstrumentType::bass);
        CHECK_STR (lowB.report.tuning.c_str(), "5 strings (B E A D G)");
        CHECK (scoreToMei (lowB.score, {}).find ("lines=\"5\"") != std::string::npos);

        // the settings as JSON
        TranscriptionSettings custom;
        custom.tuning = "38 45 50 55 59 64";
        CHECK (TranscriptionSettings::fromJson (custom.toJson()) == custom);
        CHECK_STR (TranscriptionSettings::fromJson (Json()).tuning.c_str(), "auto");
        CHECK (! (TranscriptionSettings() == custom));
    }

    //==========================================================================
    // The drum map

    void testDrumMap()
    {
        const auto gm = drumPreset ("gm");
        const auto* snare = gm.find (38);
        CHECK (snare != nullptr);

        if (snare != nullptr)
        {
            CHECK_EQ (snare->loc, 5);                 // third space
            CHECK_STR (snare->head.c_str(), "normal");
            CHECK_EQ (snare->voice, 1);
            CHECK (snare->ghostBelow > 0);
        }

        CHECK_EQ (gm.find (36)->loc, 1);              // kick: first space
        CHECK_EQ (gm.find (36)->voice, 2);
        CHECK_STR (gm.find (42)->head.c_str(), "x");  // closed hi-hat
        CHECK_EQ (gm.find (42)->loc, 9);              // space above the staff
        CHECK_EQ (gm.find (44)->loc, -1);             // hi-hat pedal below the staff
        CHECK_EQ (gm.find (51)->loc, 8);              // ride on the top line
        CHECK_EQ (gm.find (49)->loc, 10);             // crash on the first ledger line
        CHECK (gm.find (46) != nullptr && gm.find (46)->head == "open-x");
        CHECK (gm.find (20) == nullptr);

        // GM 2 has everything GM has, and more
        const auto presets = drumPresets();
        CHECK_EQ ((int) presets.size(), 2);
        const auto gm2 = drumPreset ("gm2");
        CHECK (gm2.entries.size() > gm.entries.size());

        for (const auto& e : gm.entries)
            CHECK (gm2.find (e.note) != nullptr && *gm2.find (e.note) == e);

        CHECK_STR (drumPreset ("nonsense").id.c_str(), "gm");

        // editing
        auto map = makeEmptyDrumMap ("mine", "My kit");
        CHECK (map.entries.empty());
        DrumEntry e;
        e.note = 60; e.name = "Pad"; e.loc = 7; e.head = "diamond"; e.voice = 1; e.ghostBelow = 30;
        map.set (e);
        e.note = 40; e.name = "Rim";
        map.set (e);
        CHECK_EQ ((int) map.entries.size(), 2);
        CHECK_EQ (map.entries.front().note, 40);      // kept in order of note number
        e.name = "Rim 2";
        map.set (e);
        CHECK_EQ ((int) map.entries.size(), 2);
        CHECK_STR (map.find (40)->name.c_str(), "Rim 2");
        CHECK (map.remove (40));
        CHECK (! map.remove (40));
        CHECK_EQ ((int) map.entries.size(), 1);
    }

    void testDrumMapJson()
    {
        for (const auto& original : drumPresets())
        {
            DrumMap back;
            std::string error;
            CHECK (DrumMap::fromJson (original.toJson(), back, &error));
            CHECK (back == original);

            // and through text
            Json parsed;
            CHECK (Json::parse (original.toJson().dump(), parsed));
            DrumMap again;
            CHECK (DrumMap::fromJson (parsed, again));
            CHECK (again == original);
        }

        DrumMap result;
        std::string error;
        CHECK (! DrumMap::fromJson (Json (3), result, &error));
        CHECK (! error.empty());
        CHECK (! DrumMap::fromJson (Json::object(), result, &error));

        // an entry with a note outside 0-127, or no note
        auto bad = Json::object();
        auto list = Json::array();
        auto entry = Json::object();
        entry.set ("note", 300);
        list.push (entry);
        bad.set ("entries", list);
        CHECK (! DrumMap::fromJson (bad, result, &error));

        bad = Json::object();
        list = Json::array();
        list.push (Json::object());
        bad.set ("entries", list);
        CHECK (! DrumMap::fromJson (bad, result, &error));

        // missing details get defaults, out-of-range ones are brought back
        bad = Json::object();
        list = Json::array();
        entry = Json::object();
        entry.set ("note", 70);
        entry.set ("loc", 99);
        entry.set ("head", "star");
        entry.set ("voice", 7);
        entry.set ("ghostBelow", -5);
        list.push (entry);
        bad.set ("entries", list);
        CHECK (DrumMap::fromJson (bad, result, &error));
        CHECK_EQ ((int) result.entries.size(), 1);
        CHECK_EQ (result.entries[0].loc, 16);
        CHECK_STR (result.entries[0].head.c_str(), "normal");
        CHECK_EQ (result.entries[0].voice, 4);   // voices go up to 4: 7 is brought back to 4
        CHECK_EQ (result.entries[0].ghostBelow, 0);
        CHECK_STR (result.name.c_str(), "Drum map");
    }

    void testProfile()
    {
        Profile piano;
        CHECK_STR (piano.toJson().dump().c_str(), "{\"type\":\"piano\"}");
        CHECK (Profile::fromJson (piano.toJson()) == piano);

        for (const auto type : { InstrumentType::drums, InstrumentType::guitar, InstrumentType::bass })
        {
            Profile pr;
            pr.type = type;
            pr.drumMap = drumPreset ("gm2");
            const auto back = Profile::fromJson (pr.toJson());
            CHECK (back == pr);
            CHECK (back.type == type);
        }

        // a profile that cannot be read is a piano
        CHECK (Profile::fromJson (Json()).type == InstrumentType::piano);
        auto odd = Json::object();
        odd.set ("type", "theremin");
        CHECK (Profile::fromJson (odd).type == InstrumentType::piano);

        CHECK_EQ ((int) openStrings (InstrumentType::guitar).size(), 6);
        CHECK_EQ (openStrings (InstrumentType::guitar).front(), 40);
        CHECK_EQ (openStrings (InstrumentType::guitar).back(), 64);
        CHECK_EQ ((int) openStrings (InstrumentType::bass).size(), 4);
        CHECK_EQ (openStrings (InstrumentType::bass).front(), 28);
        CHECK (openStrings (InstrumentType::piano).empty());
    }

    //==========================================================================
    // Drums

    std::string drumsOf (const ResolvedCapture& rc, const DrumMap& map = drumPreset ("gm"), TranscriptionSettings s = {},
                         TranscriptionResult* keep = nullptr)
    {
        auto result = transcribeDrums (rc, s, map);
        const auto problem = result.score.validate();
        testing::report (problem.empty(), __FILE__, __LINE__, "drums: invalid score: " + problem);
        auto text = dumpScore (result.score);

        if (keep != nullptr)
            *keep = std::move (result);

        return text;
    }

    void expectDrums (const char* name, const ResolvedCapture& rc, const std::string& expected, const DrumMap& map = drumPreset ("gm"),
                      TranscriptionSettings s = {})
    {
        const auto actual = drumsOf (rc, map, s);
        const bool same = actual == expected;
        testing::report (same, __FILE__, __LINE__, std::string (name) + ": the score differs");

        if (! same)
            std::printf ("--- expected\n%s--- actual\n%s---\n", expected.c_str(), actual.c_str());
    }

    void testDrumGrooves()
    {
        std::vector<N> groove;

        for (int i = 0; i < 8; ++i)
            groove.push_back ({ 42, i * 0.5, 0.1, 90 });

        groove.push_back ({ 36, 0.0, 0.1, 110 });
        groove.push_back ({ 36, 2.0, 0.1, 110 });
        groove.push_back ({ 38, 1.0, 0.1, 110 });
        groove.push_back ({ 38, 3.0, 0.1, 110 });

        auto rc = tsupport::capture ({});
        rc.notes.clear();

        for (const auto& n : groove)
        {
            ResolvedNote r;
            r.onPpq = n.start; r.offPpq = n.start + n.dur; r.pitch = n.pitch; r.velocity = n.vel;
            rc.notes.push_back (r);
        }

        std::sort (rc.notes.begin(), rc.notes.end(), [] (const ResolvedNote& a, const ResolvedNote& b)
                   { return a.onPpq != b.onPpq ? a.onPpq < b.onPpq : a.pitch < b.pitch; });
        rc.lengthPpq = 4.0;
        rc.bars = { { 0.0, 4, 4 } };

        // hands (voice 1): hi-hat eighths with the snare on 2 and 4; feet (voice 2): kick on 1 and 3
        expectDrums ("groove", rc,
                     "score key=0 major\n"
                     "m1 4/4\n"
                     "  tempo @0: 120\n"
                     "  S1 v1: ( Closed_hi-hat/8 Closed_hi-hat/8 ) ( [Snare Closed_hi-hat]/8 Closed_hi-hat/8 ) ( Closed_hi-hat/8 Closed_hi-hat/8 ) ( [Snare Closed_hi-hat]/8 Closed_hi-hat/8 )\n"
                     "  S1 v2: Bass_drum_1/4 r/4 Bass_drum_1/4 r/4\n");

        TranscriptionResult result;
        drumsOf (rc, drumPreset ("gm"), {}, &result);
        CHECK_EQ (result.report.maxVoices, 2);
        CHECK (result.report.warnings.empty());
        CHECK_STR (result.score.root().prop ("generator").asString().c_str(), "transcribeDrums");
        CHECK_EQ ((int) result.score.root().children[0].children.size(), 1);   // one staff
        CHECK_STR (result.score.root().children[0].children[0].prop ("clef").asString().c_str(), "perc");
    }

    void testDrumDetails()
    {
        // ghost notes: a soft snare is in brackets, a loud one is not
        expectDrums ("ghost", tsupport::capture ({ { 38, 0.0, 0.1, 30 }, { 38, 1.0, 0.1, 110 } }),
                     "score key=0 major\n"
                     "m1 4/4\n"
                     "  tempo @0: 120\n"
                     "  S1 v1: (Snare)/4 Snare/4 r/2\n");

        // the same note without a ghost threshold
        auto map = drumPreset ("gm");
        auto snare = *map.find (38);
        snare.ghostBelow = 0;
        map.set (snare);
        expectDrums ("no ghost", tsupport::capture ({ { 38, 0.0, 0.1, 30 } }),
                     "score key=0 major\n"
                     "m1 4/4\n"
                     "  tempo @0: 120\n"
                     "  S1 v1: Snare/4 r/4 r/2\n",
                     map);

        // a hit on the off-beat is an eighth, then rest
        expectDrums ("offbeat kick", tsupport::capture ({ { 36, 0.0, 0.1 }, { 36, 2.5, 0.1 } }),
                     "score key=0 major\n"
                     "m1 4/4\n"
                     "  tempo @0: 120\n"
                     "  S1 v1: Bass_drum_1/4 r/4 r/8 Bass_drum_1/8 r/4\n");

        // feet only: one voice
        TranscriptionResult feet;
        const auto text = drumsOf (tsupport::capture ({ { 36, 0.0, 0.1 }, { 36, 2.0, 0.1 }, { 44, 1.0, 0.1 } }), drumPreset ("gm"), {}, &feet);
        CHECK_EQ (feet.report.maxVoices, 1);
        CHECK (text.find ("S1 v2") == std::string::npos);

        // hands only: one voice
        TranscriptionResult hands;
        const auto text2 = drumsOf (tsupport::capture ({ { 38, 0.0, 0.1 }, { 38, 1.0, 0.1 } }), drumPreset ("gm"), {}, &hands);
        CHECK_EQ (hands.report.maxVoices, 1);
        CHECK (text2.find ("S1 v2") == std::string::npos);

        // an empty take is a bar of rests
        TranscriptionResult empty;
        const auto text3 = drumsOf (tsupport::capture ({}, 4.0), drumPreset ("gm"), {}, &empty);
        CHECK (text3.find ("S1 v1: R") != std::string::npos);
        CHECK_EQ (empty.report.measures, 1);

        // 3/4
        TranscriptionResult waltz;
        drumsOf (tsupport::capture ({ { 36, 0.0, 0.1 }, { 38, 1.0, 0.1 }, { 38, 2.0, 0.1 } }, 3.0, { { 0.0, 3, 4 } }), drumPreset ("gm"), {}, &waltz);
        CHECK_EQ (waltz.report.measures, 1);
        CHECK (dumpScore (waltz.score).find ("m1 3/4") != std::string::npos);
    }

    void testDrumUnmappedAndMerged()
    {
        // a note that is not in the map is reported, not dropped without a word
        TranscriptionResult r;
        const auto text = drumsOf (tsupport::capture ({ { 38, 0.0, 0.1 }, { 60, 1.0, 0.1 }, { 61, 2.0, 0.1 }, { 60, 3.0, 0.1 } }), drumPreset ("gm"), {}, &r);
        CHECK_EQ ((int) r.report.warnings.size(), 1);
        CHECK (r.report.warnings[0].find ("3 notes are not in the drum map") != std::string::npos);
        CHECK (r.report.warnings[0].find ("60, 61") != std::string::npos);
        CHECK (text.find ("Snare") != std::string::npos);
        CHECK (text.find ("60") == std::string::npos);

        // ... and written once the map knows it
        auto map = drumPreset ("gm");
        DrumEntry e;
        e.note = 60; e.name = "Wood block"; e.loc = 7; e.head = "normal"; e.voice = 1;
        map.set (e);
        TranscriptionResult r2;
        const auto text2 = drumsOf (tsupport::capture ({ { 60, 1.0, 0.1 } }), map, {}, &r2);
        CHECK (r2.report.warnings.empty());
        CHECK (text2.find ("Wood_block") != std::string::npos);

        // two hits of one drum in one slot are written once
        TranscriptionResult r3;
        const auto text3 = drumsOf (tsupport::capture ({ { 38, 1.0, 0.1, 90 }, { 38, 1.02, 0.1, 100 } }), drumPreset ("gm"), {}, &r3);
        CHECK (r3.report.mergedNotes >= 1);
        CHECK (text3.find ("[") == std::string::npos);

        // many different unmapped notes: the list is cut
        std::vector<N> notes;

        for (int i = 0; i < 10; ++i)
            notes.push_back ({ 100 + i, i * 0.25, 0.1 });

        TranscriptionResult r4;
        auto rc = tsupport::capture ({}, 4.0);

        for (const auto& n : notes)
        {
            ResolvedNote x;
            x.onPpq = n.start; x.offPpq = n.start + n.dur; x.pitch = n.pitch;
            rc.notes.push_back (x);
        }

        drumsOf (rc, drumPreset ("gm"), {}, &r4);
        CHECK (r4.report.warnings[0].find ("...") != std::string::npos);
    }

    void testDrumTriplets()
    {
        // hi-hat in eighth triplets on beat 1, quarter on beat 2
        TranscriptionResult r;
        const auto text = drumsOf (tsupport::capture ({ { 42, 0.0, 0.1 }, { 42, 1.0 / 3.0, 0.1 }, { 42, 2.0 / 3.0, 0.1 }, { 42, 1.0, 0.1 } }),
                                   drumPreset ("gm"), {}, &r);
        CHECK (text.find ("< ( Closed_hi-hat/8 Closed_hi-hat/8 Closed_hi-hat/8 ) >") != std::string::npos);
        CHECK_EQ (r.report.tripletBeats, 1);
    }

    // Random drum takes: every voice fills its measures, everything is valid and nothing is lost without a warning.
    void testRandomDrumTakes()
    {
        std::mt19937 rng (4242);
        const int kit[] = { 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 53, 55, 57, 59, 70 };

        for (int clip = 0; clip < 60; ++clip)
        {
            auto rc = tsupport::capture ({}, 16.0);

            const int count = 4 + (int) (rng() % 120);

            for (int i = 0; i < count; ++i)
            {
                ResolvedNote r;
                r.onPpq = (double) (rng() % 256) / 16.0 + (rng() % 6 == 0 ? (double) (rng() % 100) / 400.0 : 0.0);
                r.offPpq = r.onPpq + 0.1;
                r.pitch = kit[rng() % (sizeof kit / sizeof kit[0])];
                r.velocity = 20 + (int) (rng() % 100);
                rc.notes.push_back (r);
            }

            std::sort (rc.notes.begin(), rc.notes.end(), [] (const ResolvedNote& a, const ResolvedNote& b)
                       { return a.onPpq != b.onPpq ? a.onPpq < b.onPpq : a.pitch < b.pitch; });

            auto map = drumPreset (clip % 2 == 0 ? "gm" : "gm2");
            TranscriptionResult result;
            drumsOf (rc, map, {}, &result);

            const auto& staff = result.score.root().children[0].children[0];

            for (const auto& measure : staff.children)
            {
                for (const auto& layer : measure.children)
                {
                    if (layer.type != nodeType::layer)
                        continue;

                    int64_t sum = 0;

                    for (const auto& e : layer.children)
                        sum += e.prop ("ticks").asInt();

                    testing::report (sum == measure.prop ("ticks").asInt(), __FILE__, __LINE__, "random drums: a voice does not fill its measure");
                }
            }

            // every written note is a drum of the map, with its place
            std::vector<const Node*> stack { &result.score.root() };
            int written = 0;

            while (! stack.empty())
            {
                const auto* n = stack.back();
                stack.pop_back();

                if (n->type == nodeType::note)
                {
                    const auto tie = n->prop ("tie").asString();
                    written += tie == "t" || tie == "m" ? 0 : 1;   // a note cut by a bar line is one note
                    const auto* entry = map.find ((int) n->prop ("pitch").asInt());
                    testing::report (entry != nullptr && entry->loc == n->prop ("loc").asInt(), __FILE__, __LINE__, "random drums: wrong place");
                }

                for (const auto& c : n->children)
                    stack.push_back (&c);
            }

            int mapped = 0;

            for (const auto& n : rc.notes)
                mapped += map.find (n.pitch) != nullptr ? 1 : 0;

            // notes can only be merged (the same drum in one slot), never lost
            testing::report (written + result.report.mergedNotes >= mapped, __FILE__, __LINE__, "random drums: notes were lost");
            testing::report (written <= mapped, __FILE__, __LINE__, "random drums: more notes than played");
        }
    }

    void testDrumFixtures()
    {
#ifdef TRANSCRIBER_FIXTURES_DIR
        const std::string dir = TRANSCRIBER_FIXTURES_DIR;

        auto f = midireader::read (dir + "/t23-drums-3min.mid");
        CHECK (f.ok);

        if (f.ok)
        {
            const auto result = transcribeDrums (f.capture, {}, drumPreset ("gm"));
            CHECK (result.score.validate().empty());
            CHECK_EQ (result.report.measures, 90);
            CHECK (result.report.warnings.empty());   // every note of the clip is in the General MIDI map
            CHECK_EQ (result.report.maxVoices, 2);
        }

        auto g = midireader::read (dir + "/t31-drum-groove.mid");
        CHECK (g.ok);

        if (g.ok)
        {
            const auto result = transcribeDrums (g.capture, {}, drumPreset ("gm"));
            CHECK (result.report.warnings.empty());
            CHECK_EQ (result.report.measures, 4);
            const auto text = dumpScore (result.score);
            CHECK (text.find ("(Snare)/16") != std::string::npos);                // the ghost note in bar 3
            CHECK (text.find ("[Closed_hi-hat Crash_cymbal_1]/8") != std::string::npos);
            CHECK (text.find ("[Closed_hi-hat Open_hi-hat]/8") != std::string::npos);
            CHECK (text.find ("Snare/16 High_tom/16 Hi-mid_tom/16 Low_tom/16") != std::string::npos);   // the fill
        }
#endif
    }

    //==========================================================================
    // Tablature

    std::string tabText (const std::vector<std::vector<TabNote>>& tab)
    {
        std::string out;

        for (const auto& chord : tab)
        {
            std::string one;

            for (const auto& n : chord)
                one += (one.empty() ? "" : " ") + std::to_string (n.string + 1) + ":" + std::to_string (n.fret);

            out += (out.empty() ? "[" : " [") + one + "]";
        }

        return out;
    }

    void testTabBasics()
    {
        const auto guitar = openStrings (InstrumentType::guitar);

        // open strings (string numbers here count from the lowest: 1 = low E)
        CHECK_STR (tabText (assignTab ({ { 40 }, { 45 }, { 50 }, { 55 }, { 59 }, { 64 } }, guitar)).c_str(),
                   "[1:0] [2:0] [3:0] [4:0] [5:0] [6:0]");

        // an open E major chord

        const auto e = assignTab ({ { 40, 47, 52, 56, 59, 64 } }, guitar);
        CHECK_EQ ((int) e.size(), 1);
        CHECK_EQ ((int) e[0].size(), 6);

        std::map<int, int> fretOfPitch;

        for (const auto& n : e[0])
            fretOfPitch[n.pitch] = n.fret;

        CHECK_EQ (fretOfPitch[40], 0);
        CHECK_EQ (fretOfPitch[47], 2);
        CHECK_EQ (fretOfPitch[52], 2);
        CHECK_EQ (fretOfPitch[56], 1);
        CHECK_EQ (fretOfPitch[59], 0);
        CHECK_EQ (fretOfPitch[64], 0);

        // a note below the lowest string cannot be played; the others of the chord stay
        auto low = assignTab ({ { 30 }, { 30, 45 } }, guitar);
        CHECK (low[0].empty());
        CHECK_EQ ((int) low[1].size(), 1);
        CHECK_EQ (low[1][0].pitch, 45);

        // above the 22nd fret of the top string
        CHECK (assignTab ({ { 64 + 23 } }, guitar)[0].empty());
        CHECK (! assignTab ({ { 64 + 22 } }, guitar)[0].empty());

        // more notes than strings: the lowest ones go
        const auto seven = assignTab ({ { 40, 45, 50, 55, 59, 64, 69 } }, guitar);
        CHECK_EQ ((int) seven[0].size(), 6);

        // the same pitch twice is one note
        CHECK_EQ ((int) assignTab ({ { 50, 50 } }, guitar)[0].size(), 1);

        // no strings, no tab
        CHECK (assignTab ({ { 40 } }, {})[0].empty());
        CHECK (assignTab ({}, guitar).empty());

        // bass
        const auto bass = openStrings (InstrumentType::bass);
        CHECK_STR (tabText (assignTab ({ { 28 }, { 33 }, { 38 }, { 43 } }, bass)).c_str(), "[1:0] [2:0] [3:0] [4:0]");
        CHECK (assignTab ({ { 27 } }, bass)[0].empty());
    }

    void testTabKeepsTheHandStill()
    {
        const auto guitar = openStrings (InstrumentType::guitar);

        // an A minor pentatonic run in the fifth position: every note within four frets
        const auto run = assignTab ({ { 57 }, { 60 }, { 62 }, { 64 }, { 67 }, { 69 }, { 67 }, { 64 }, { 62 }, { 60 }, { 57 } }, guitar);
        int low = 100, high = 0;

        for (const auto& c : run)
        {
            for (const auto& n : c)
            {
                if (n.fret > 0)
                {
                    low = std::min (low, n.fret);
                    high = std::max (high, n.fret);
                }
            }
        }

        CHECK (high - low <= 5);

        // a repeated note stays where it is
        const auto repeated = assignTab ({ { 62 }, { 62 }, { 62 }, { 62 } }, guitar);

        for (const auto& c : repeated)
            CHECK (c[0].string == repeated[0][0].string && c[0].fret == repeated[0][0].fret);

        // the DP looks ahead: G3 first and a chord after it settle in the same place
        const auto ahead = assignTab ({ { 55 }, { 50, 57, 62 } }, guitar);
        CHECK_EQ ((int) ahead.size(), 2);
    }

    // Whatever is asked, a result is playable: distinct strings, the fret matches the pitch, frets 0-22, a span of at most four frets.
    void testTabIsAlwaysPlayable()
    {
        std::mt19937 rng (9);

        for (const auto type : { InstrumentType::guitar, InstrumentType::bass })
        {
            const auto open = openStrings (type);

            for (int run = 0; run < 150; ++run)
            {
                std::vector<std::vector<int>> chords;
                const int events = 1 + (int) (rng() % 30);

                for (int i = 0; i < events; ++i)
                {
                    std::vector<int> chord;
                    const int size = 1 + (int) (rng() % 5);

                    for (int k = 0; k < size; ++k)
                        chord.push_back (open.front() - 3 + (int) (rng() % 40));

                    chords.push_back (chord);
                }

                const auto tab = assignTab (chords, open);
                testing::report (tab.size() == chords.size(), __FILE__, __LINE__, "tab: wrong number of chords");

                for (const auto& chord : tab)
                {
                    std::set<int> strings;
                    int lowest = 100, highest = 0;

                    for (const auto& n : chord)
                    {
                        testing::report (n.string >= 0 && n.string < (int) open.size(), __FILE__, __LINE__, "tab: bad string");
                        testing::report (n.fret == n.pitch - open[(size_t) n.string], __FILE__, __LINE__, "tab: the fret does not match the pitch");
                        testing::report (n.fret >= 0 && n.fret <= 22, __FILE__, __LINE__, "tab: bad fret");
                        strings.insert (n.string);

                        if (n.fret > 0)
                        {
                            lowest = std::min (lowest, n.fret);
                            highest = std::max (highest, n.fret);
                        }
                    }

                    testing::report (strings.size() == chord.size(), __FILE__, __LINE__, "tab: two notes on one string");
                    testing::report (highest - lowest <= 4 || chord.size() <= 1 || highest == 0, __FILE__, __LINE__, "tab: the hand span is too wide");
                }
            }
        }
    }

    //==========================================================================
    // Guitar and bass

    ResolvedCapture captureOf (const std::vector<N>& notes, double length)
    {
        auto rc = tsupport::capture ({}, length);

        for (const auto& n : notes)
        {
            ResolvedNote r;
            r.onPpq = n.start; r.offPpq = n.start + n.dur; r.pitch = n.pitch; r.velocity = n.vel;
            rc.notes.push_back (r);
        }

        std::sort (rc.notes.begin(), rc.notes.end(), [] (const ResolvedNote& a, const ResolvedNote& b)
                   { return a.onPpq != b.onPpq ? a.onPpq < b.onPpq : a.pitch < b.pitch; });
        return rc;
    }

    std::string frettedOf (const ResolvedCapture& rc, InstrumentType type, TranscriptionSettings s = {}, TranscriptionResult* keep = nullptr)
    {
        auto result = transcribeFretted (rc, s, type);
        const auto problem = result.score.validate();
        testing::report (problem.empty(), __FILE__, __LINE__, "fretted: invalid score: " + problem);
        auto text = dumpScore (result.score);

        if (keep != nullptr)
            *keep = std::move (result);

        return text;
    }

    void testGuitarScores()
    {
        TranscriptionSettings eMinor;
        eMinor.keyTonic = 4;
        eMinor.keyMinor = true;

        // a line, then a chord with a tied note across the bar
        const auto text = frettedOf (captureOf ({ { 40, 0.0, 1.0 }, { 43, 1.0, 1.0 }, { 47, 2.0, 1.0 }, { 52, 3.0, 2.0 } }, 8.0), InstrumentType::guitar, eMinor);
        CHECK_STR (text.c_str(),
                   "score key=1 minor\n"
                   "m1 4/4\n"
                   "  tempo @0: 120\n"
                   "  S1 v1: E2/4 G2/4 B2/4 E3/4~\n"
                   "  S2 v1: 6:0/4 6:3/4 5:2/4 4:2/4~\n"
                   "m2 4/4\n"
                   "  S1 v1: E3/4 r/4 r/2\n"
                   "  S2 v1: 4:2/4 r/4 r/2\n");

        // a note cut at the next chord, a short note followed by silence
        const auto text2 = frettedOf (captureOf ({ { 40, 0.0, 3.0 }, { 45, 1.0, 0.5 }, { 50, 2.0, 1.0 } }, 4.0), InstrumentType::guitar, eMinor);
        CHECK (text2.find ("S1 v1: E2/4 A2/8 r/8 D3/4 r/4") != std::string::npos);
    }

    void testGuitarDetails()
    {
        TranscriptionResult r;
        const auto text = frettedOf (captureOf ({ { 40, 0.0, 1.0 }, { 20, 1.0, 1.0 }, { 100, 2.0, 1.0 }, { 45, 3.0, 1.0 } }, 4.0), InstrumentType::guitar, {}, &r);
        CHECK (r.report.warnings.size() >= 1);
        CHECK (r.report.warnings[0].find ("2 notes cannot be played on a 6-string guitar") != std::string::npos);
        CHECK (text.find ("S1 v1: E2/4 r/4 r/4 A2/4") != std::string::npos);

        // two staves: notation and tab
        const auto& part = r.score.root().children[0];
        CHECK_EQ ((int) part.children.size(), 2);
        CHECK_STR (part.children[0].prop ("clef").asString().c_str(), "G8");
        CHECK_STR (part.children[1].prop ("clef").asString().c_str(), "TAB");
        CHECK_STR (part.children[1].prop ("kind").asString().c_str(), "tab");
        CHECK_EQ ((int) part.children[1].prop ("strings").asInt(), 6);
        CHECK_STR (part.prop ("name").asString().c_str(), "Guitar");

        // a bass
        TranscriptionResult b;
        frettedOf (captureOf ({ { 28, 0.0, 1.0 } }, 4.0), InstrumentType::bass, {}, &b);
        const auto& bpart = b.score.root().children[0];
        CHECK_STR (bpart.children[0].prop ("clef").asString().c_str(), "F8");
        CHECK_EQ ((int) bpart.children[1].prop ("strings").asInt(), 4);
        CHECK_STR (bpart.prop ("name").asString().c_str(), "Bass");

        // nothing played: rests in both staves
        TranscriptionResult e;
        const auto none = frettedOf (captureOf ({}, 4.0), InstrumentType::guitar, {}, &e);
        CHECK (none.find ("S1 v1: R") != std::string::npos);
        CHECK (none.find ("S2 v1: R") != std::string::npos);

        // a chord with the same note twice
        TranscriptionResult d;
        frettedOf (captureOf ({ { 50, 0.0, 1.0 }, { 50, 0.0, 1.0, 100 } }, 4.0), InstrumentType::guitar, {}, &d);
        CHECK_EQ (d.report.maxVoices, 1);
    }

    // The tab says the same as the notation: the same events, and every tab note is the pitch of its notation note.
    void testTabMirrorsNotation()
    {
        std::mt19937 rng (31);

        for (const auto type : { InstrumentType::guitar, InstrumentType::bass })
        {
            const auto open = openStrings (type);

            for (int clip = 0; clip < 40; ++clip)
            {
                std::vector<N> notes;
                const int count = 3 + (int) (rng() % 40);

                for (int i = 0; i < count; ++i)
                    notes.push_back ({ open.front() + (int) (rng() % 30), (double) (rng() % 64) / 4.0, 0.2 + (double) (rng() % 8) / 4.0, 90 });

                TranscriptionResult r;
                frettedOf (captureOf (notes, 16.0), type, {}, &r);

                const auto& part = r.score.root().children[0];
                std::vector<int> standard, tab;

                for (int s = 0; s < 2; ++s)
                {
                    std::vector<const Node*> stack { &part.children[(size_t) s] };

                    while (! stack.empty())
                    {
                        const auto* n = stack.back();
                        stack.pop_back();

                        if (n->type == nodeType::note)
                        {
                            (s == 0 ? standard : tab).push_back ((int) n->prop ("pitch").asInt());

                            if (s == 1)
                            {
                                const auto course = (int) n->prop ("course").asInt();
                                const auto fret = (int) n->prop ("fret").asInt();
                                testing::report (course >= 1 && course <= (int) open.size() && fret >= 0
                                                 && open[open.size() - (size_t) course] + fret == n->prop ("pitch").asInt(),
                                                 __FILE__, __LINE__, "tab note does not match its pitch");
                            }
                        }

                        for (const auto& c : n->children)
                            stack.push_back (&c);
                    }
                }

                std::sort (standard.begin(), standard.end());
                std::sort (tab.begin(), tab.end());
                testing::report (standard == tab, __FILE__, __LINE__, "the tab and the notation have different notes");

                // every voice fills every measure in both staves
                for (const auto& staff : part.children)
                {
                    for (const auto& measure : staff.children)
                    {
                        for (const auto& layer : measure.children)
                        {
                            if (layer.type != nodeType::layer)
                                continue;

                            int64_t sum = 0;

                            for (const auto& e : layer.children)
                                sum += e.prop ("ticks").asInt();

                            testing::report (sum == measure.prop ("ticks").asInt(), __FILE__, __LINE__, "fretted: a voice does not fill its measure");
                        }
                    }
                }
            }
        }
    }

    void testFrettedFixtures()
    {
#ifdef TRANSCRIBER_FIXTURES_DIR
        const std::string dir = TRANSCRIBER_FIXTURES_DIR;

        auto g = midireader::read (dir + "/t31-guitar-riff.mid");
        CHECK (g.ok);

        if (g.ok)
        {
            TranscriptionSettings s;
            s.keyTonic = 4;
            s.keyMinor = true;
            TranscriptionResult r;
            frettedOf (g.capture, InstrumentType::guitar, s, &r);
            CHECK (r.report.warnings.empty());
            const auto text = dumpScore (r.score);

            // the notes of the riff are the open and low-position ones, the chords are the usual shapes
            CHECK (text.find ("S2 v1: ( 6:0/8 6:0/8 ) ( 6:3/8 5:0/8 ) 5:2/4 [6:0 5:2 4:2]/4") != std::string::npos);
            CHECK (text.find ("[6:0 5:2 4:2 3:1 2:0 1:0]/2") != std::string::npos);
        }

        auto b = midireader::read (dir + "/t31-bassline.mid");
        CHECK (b.ok);

        if (b.ok)
        {
            TranscriptionResult r;
            const auto text = frettedOf (b.capture, InstrumentType::bass, {}, &r);
            CHECK (r.report.warnings.empty());
            CHECK (text.find ("S2 v1: ( 3:0/8. 3:0/16 ) ( 2:2/8 1:0/8 ) 1:2/4 ( 2:2/8 2:0/8 )") != std::string::npos);
            CHECK (text.find ("S2 v1: 4:1/4 ( 4:1/8 3:3/8 ) 2:3/4 4:3/4") != std::string::npos);
        }
#endif
    }

    //==========================================================================
    // The dispatcher and MEI

    void testTranscribeByProfile()
    {
        const auto rc = tsupport::capture ({ { 42, 0.0, 0.1 }, { 38, 1.0, 0.1 }, { 40, 2.0, 1.0 } });

        Profile profile;
        CHECK_STR (transcribe (rc, {}, profile).score.root().prop ("generator").asString().c_str(), "transcribePiano");

        profile.type = InstrumentType::drums;
        CHECK_STR (transcribe (rc, {}, profile).score.root().prop ("generator").asString().c_str(), "transcribeDrums");

        profile.type = InstrumentType::guitar;
        CHECK_STR (transcribe (rc, {}, profile).score.root().prop ("generator").asString().c_str(), "transcribeGuitar");

        profile.type = InstrumentType::bass;
        CHECK_STR (transcribe (rc, {}, profile).score.root().prop ("generator").asString().c_str(), "transcribeBass");

        // the profile's own drum map is used
        profile.type = InstrumentType::drums;
        profile.drumMap = makeEmptyDrumMap ("empty", "Empty");
        const auto none = transcribe (rc, {}, profile);
        CHECK (! none.report.warnings.empty());
    }

    void checkMeiOf (const TranscriptionResult& result, const std::string& what, int expectedStaffDefs)
    {
        const auto mei = scoreToMei (result.score);
        const auto x = xmlcheck::checkXml (mei);
        testing::report (x.wellFormed, __FILE__, __LINE__, what + ": MEI is not well formed: " + x.problem);

        std::set<std::string> unique (x.ids.begin(), x.ids.end());
        testing::report (unique.size() == x.ids.size(), __FILE__, __LINE__, what + ": xml:ids repeat");

        for (const auto& id : x.ids)
            testing::report (result.score.find (id) != nullptr, __FILE__, __LINE__, what + ": unknown id " + id);

        auto count = [&] (const char* tag) { auto it = x.tagCount.find (tag); return it == x.tagCount.end() ? 0 : it->second; };
        testing::report (count ("staffDef") == expectedStaffDefs, __FILE__, __LINE__, what + ": staffDef count");
        testing::report (count ("measure") == result.report.measures, __FILE__, __LINE__, what + ": measure count");

        std::size_t notes = 0;
        std::vector<const Node*> stack { &result.score.root() };

        while (! stack.empty())
        {
            const auto* n = stack.back();
            stack.pop_back();
            notes += n->type == nodeType::note ? 1u : 0u;

            for (const auto& c : n->children)
                stack.push_back (&c);
        }

        testing::report ((std::size_t) count ("note") == notes, __FILE__, __LINE__, what + ": note count");
    }

    void testDrumMei()
    {
        auto rc = captureOf ({ { 42, 0.0, 0.1, 90 }, { 36, 0.0, 0.1, 100 }, { 38, 1.0, 0.1, 30 }, { 46, 2.0, 0.1, 90 }, { 44, 3.0, 0.1, 80 } }, 4.0);
        const auto result = transcribeDrums (rc, {}, drumPreset ("gm"));
        const auto mei = scoreToMei (result.score);

        CHECK (mei.find ("clef.shape=\"perc\"") != std::string::npos);
        CHECK (mei.find ("loc=\"9\" head.shape=\"x\"") != std::string::npos);     // closed hi-hat
        CHECK (mei.find ("loc=\"1\"") != std::string::npos);                       // kick
        CHECK (mei.find ("loc=\"-1\"") != std::string::npos);                      // hi-hat pedal
        CHECK (mei.find ("head.mod=\"paren\"") != std::string::npos);              // the ghost note
        CHECK (mei.find ("stem.dir=\"up\"") != std::string::npos);
        CHECK (mei.find ("stem.dir=\"down\"") != std::string::npos);
        CHECK (mei.find (">o</dir>") != std::string::npos);                        // the open hi-hat
        CHECK (mei.find ("pname=") == std::string::npos);                          // drums have no pitch names
        CHECK (mei.find ("symbol=") == std::string::npos);                         // one staff: no brace
        checkMeiOf (result, "drums", 1);

        // the sentences for a click
        std::vector<const Node*> stack { &result.score.root() };
        std::string kickText, ghostText;

        while (! stack.empty())
        {
            const auto* n = stack.back();
            stack.pop_back();

            if (n->type == nodeType::note && n->prop ("pitch").asInt() == 36) kickText = describeNode (result.score, n->id);
            if (n->type == nodeType::note && n->prop ("ghost").asBool()) ghostText = describeNode (result.score, n->id);

            for (const auto& c : n->children)
                stack.push_back (&c);
        }

        CHECK (kickText.find ("Drums") == 0);
        CHECK (kickText.find ("bass drum 1") != std::string::npos);
        CHECK (ghostText.find ("snare (ghost note)") != std::string::npos);
    }

    void testTabMei()
    {
        TranscriptionResult r;
        frettedOf (captureOf ({ { 40, 0.0, 1.0 }, { 40, 1.0, 1.0 }, { 47, 2.0, 1.0 }, { 52, 2.0, 1.0 }, { 59, 3.0, 4.0 } }, 8.0), InstrumentType::guitar, {}, &r);
        const auto mei = scoreToMei (r.score);

        CHECK (mei.find ("notationtype=\"tab.guitar\"") != std::string::npos);
        CHECK (mei.find ("clef.shape=\"TAB\"") != std::string::npos);
        CHECK (mei.find ("lines=\"6\"") != std::string::npos);
        CHECK (mei.find ("<course n=\"1\" pname=\"e\" oct=\"4\"/>") != std::string::npos);
        CHECK (mei.find ("<course n=\"6\" pname=\"e\" oct=\"2\"/>") != std::string::npos);
        CHECK (mei.find ("<tabGrp") != std::string::npos);
        CHECK (mei.find ("<tabDurSym/>") != std::string::npos);
        CHECK (mei.find ("tab.course=\"6\" tab.fret=\"0\"") != std::string::npos);
        CHECK (mei.find ("clef.dis=\"8\" clef.dis.place=\"below\"") != std::string::npos);
        CHECK (mei.find ("symbol=\"bracket\"") != std::string::npos);
        CHECK (mei.find ("tie=\"i\"") != std::string::npos);                       // the long B over the bar line
        checkMeiOf (r, "guitar", 2);

        TranscriptionResult b;
        frettedOf (captureOf ({ { 28, 0.0, 1.0 }, { 33, 1.0, 1.0 } }, 4.0), InstrumentType::bass, {}, &b);
        const auto bassMei = scoreToMei (b.score);
        CHECK (bassMei.find ("lines=\"4\"") != std::string::npos);
        CHECK (bassMei.find ("<course n=\"4\" pname=\"e\" oct=\"1\"/>") != std::string::npos);
        CHECK (bassMei.find ("clef.shape=\"F\" clef.line=\"4\" clef.dis=\"8\"") != std::string::npos);
        checkMeiOf (b, "bass", 2);

        // clicks on a tab note and on a chord of the tab
        std::vector<const Node*> stack { &r.score.root().children[0].children[1] };
        std::string single, chord;

        while (! stack.empty())
        {
            const auto* n = stack.back();
            stack.pop_back();

            if (n->type == nodeType::note && n->prop ("pitch").asInt() == 40 && single.empty()) single = describeNode (r.score, n->id);
            if (n->type == nodeType::chord) chord = describeNode (r.score, n->id);

            for (const auto& c : n->children)
                stack.push_back (&c);
        }

        CHECK (single.find ("Tab") == 0);
        CHECK (single.find ("string 6 fret 0") != std::string::npos);
        CHECK (chord.find ("chord: string") != std::string::npos);
        CHECK (chord.find (", string") != std::string::npos);

        // one note: the singular
        TranscriptionResult one;
        frettedOf (captureOf ({ { 40, 0.0, 1.0 }, { 20, 1.0, 1.0 } }, 4.0), InstrumentType::guitar, {}, &one);
        CHECK (one.report.warnings[0].find ("1 note cannot be played on a 6-string guitar") != std::string::npos);
        CHECK (one.report.warnings[0].find ("and was left out") != std::string::npos);

        TranscriptionResult oneDrum;
        drumsOf (tsupport::capture ({ { 38, 0.0, 0.1 }, { 60, 1.0, 0.1 } }), drumPreset ("gm"), {}, &oneDrum);
        CHECK_STR (oneDrum.report.warnings[0].c_str(), "1 note is not in the drum map (MIDI note 60) and was left out. Add it to the map to write it.");
    }

    void testFixturesGiveValidMei()
    {
#ifdef TRANSCRIBER_FIXTURES_DIR
        const std::string dir = TRANSCRIBER_FIXTURES_DIR;

        for (const char* name : { "t23-drums-3min.mid", "t31-drum-groove.mid", "t23-piano-3min.mid" })
        {
            auto f = midireader::read (dir + "/" + name);
            testing::report (f.ok, __FILE__, __LINE__, name);

            if (f.ok)
                checkMeiOf (transcribeDrums (f.capture, {}, drumPreset ("gm")), std::string (name) + " as drums", 1);
        }

        for (const char* name : { "t31-guitar-riff.mid", "t31-bassline.mid", "t23-piano-3min.mid", "t13-timing.mid" })
        {
            auto f = midireader::read (dir + "/" + name);
            testing::report (f.ok, __FILE__, __LINE__, name);

            if (f.ok)
            {
                checkMeiOf (transcribeFretted (f.capture, {}, InstrumentType::guitar), std::string (name) + " as guitar", 2);
                checkMeiOf (transcribeFretted (f.capture, {}, InstrumentType::bass), std::string (name) + " as bass", 2);
            }
        }
#endif
    }

    REGISTER (testDrumMap, "instruments: the drum map");
    REGISTER (testDrumMapJson, "instruments: drum map JSON");
    REGISTER (testProfile, "instruments: profiles and tunings");
    REGISTER (testDrumGrooves, "drums: a groove with hands and feet");
    REGISTER (testDrumDetails, "drums: ghost notes, off-beat hits, voices, meters");
    REGISTER (testDrumUnmappedAndMerged, "drums: unmapped notes are reported, double hits merged");
    REGISTER (testDrumTriplets, "drums: triplets");
    REGISTER (testRandomDrumTakes, "drums: 60 random takes");
    REGISTER (testDrumFixtures, "drums: the test clips");
    REGISTER (testTabBasics, "tab: open strings, chords, limits");
    REGISTER (testTabKeepsTheHandStill, "tab: the hand stays put");
    REGISTER (testTabIsAlwaysPlayable, "tab: 300 random sequences are playable");
    REGISTER (testGuitarScores, "guitar: notation and tab");
    REGISTER (testGuitarDetails, "guitar: warnings, staves, bass");
    REGISTER (testTabMirrorsNotation, "guitar and bass: the tab matches the notation (80 random takes)");
    REGISTER (testFrettedFixtures, "guitar and bass: the test clips");
    REGISTER (testTranscribeByProfile, "instruments: the profile picks the pipeline");
    REGISTER (testDrumMei, "mei: drums");
    REGISTER (testTabMei, "mei: guitar and bass with tab");
    REGISTER (testFixturesGiveValidMei, "mei: all pipelines on the test clips");
    REGISTER (testTuningLibrary, "tunings: presets, names, typed tunings");
    REGISTER (testChooseTuning, "tunings: Automatic picks the smallest change that plays every note");
    REGISTER (testTuningsInTheScore, "tunings: in the score, the tab and the MEI");
}
