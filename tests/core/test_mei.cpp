// Tests for the MEI writer: the file is well formed XML, every id is unique and comes from the score,
// every note is written once with its ties, beams and tuplets nested properly.

#include "TestSupport.h"
#include "TranscribeSupport.h"

#include "MidiReader.h"
#include "core/Mei.h"
#include "XmlCheck.h"
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
using xmlcheck::XmlCheck;
using xmlcheck::checkXml;

namespace
{
    size_t countNodes (const Node& n, const char* type)
    {
        size_t c = n.type == type ? 1 : 0;

        for (const auto& child : n.children)
            c += countNodes (child, type);

        return c;
    }

    void checkMei (const TranscriptionResult& result, const std::string& what)
    {
        const auto mei = scoreToMei (result.score);
        const auto x = checkXml (mei);
        testing::report (x.wellFormed, __FILE__, __LINE__, what + ": MEI is not well formed: " + x.problem);

        std::set<std::string> unique (x.ids.begin(), x.ids.end());
        testing::report (unique.size() == x.ids.size(), __FILE__, __LINE__, what + ": xml:ids repeat");

        // every id of the MEI belongs to a node of the score
        for (const auto& id : x.ids)
            testing::report (result.score.find (id) != nullptr, __FILE__, __LINE__, what + ": unknown id " + id);

        const auto& root = result.score.root();
        const auto notes = countNodes (root, nodeType::note);
        const auto rests = countNodes (root, nodeType::rest);
        const auto measures = result.report.measures;

        auto count = [&] (const char* tag) { auto it = x.tagCount.find (tag); return it == x.tagCount.end() ? 0 : it->second; };

        testing::report ((size_t) count ("note") == notes, __FILE__, __LINE__, what + ": note count differs");
        testing::report ((size_t) (count ("rest") + count ("mRest")) == rests, __FILE__, __LINE__, what + ": rest count differs");
        testing::report (count ("measure") == measures, __FILE__, __LINE__, what + ": measure count differs");
        testing::report (count ("staffDef") == 2, __FILE__, __LINE__, what + ": staffDef count differs");
    }

    void testBasics()
    {
        const auto result = transcribePiano (tsupport::capture ({ { p ("C4"), 0.0, 1.0 }, { p ("E4"), 1.0, 1.0 }, { p ("G4"), 2.0, 2.0 },
                                                                  { p ("C3"), 0.0, 4.0 } }), {});
        const auto mei = scoreToMei (result.score, { "A \"title\" & <more>", "C. Omposer" });
        const auto x = checkXml (mei);

        CHECK (x.wellFormed);
        CHECK (mei.find ("<title>A &quot;title&quot; &amp; &lt;more&gt;</title>") != std::string::npos);
        CHECK (mei.find ("<composer>C. Omposer</composer>") != std::string::npos);
        CHECK (mei.find ("meiversion=\"5.1\"") != std::string::npos);
        CHECK (mei.find ("clef.shape=\"G\" clef.line=\"2\"") != std::string::npos);
        CHECK (mei.find ("clef.shape=\"F\" clef.line=\"4\"") != std::string::npos);
        CHECK (mei.find ("right=\"end\"") != std::string::npos);
        CHECK (mei.find ("pname=\"e\" oct=\"4\"") != std::string::npos);
        checkMei (result, "basics");
    }

    void testAccidentalsAndKey()
    {
        // D major: F# and C# are in the key (no written accidental), F natural is shown
        const auto result = transcribePiano (tsupport::capture ({ { p ("D4"), 0.0, 1.0 }, { p ("F#4"), 1.0, 1.0 }, { p ("A4"), 2.0, 1.0 },
                                                                  { p ("C#5"), 3.0, 1.0 }, { p ("D5"), 4.0, 1.0 }, { p ("F4"), 5.0, 1.0 },
                                                                  { p ("A4"), 6.0, 1.0 }, { p ("D5"), 7.0, 1.0 } }), {});
        const auto mei = scoreToMei (result.score);
        CHECK (mei.find ("keysig=\"2s\"") != std::string::npos);
        CHECK (mei.find ("accid=\"n\"") != std::string::npos);
        CHECK (mei.find ("accid.ges=\"s\"") != std::string::npos);
        checkMei (result, "accidentals");
    }

    void testTiesTupletsBeamsAndRests()
    {
        // a note over the bar line, eighth triplets, sixteenths, a chord and a long rest
        const auto result = transcribePiano (tsupport::capture ({
            { p ("C5"), 3.0, 2.0 },
            { p ("E5"), 5.0, 1.0 / 3.0 }, { p ("F5"), 5.0 + 1.0 / 3.0, 1.0 / 3.0 }, { p ("G5"), 5.0 + 2.0 / 3.0, 1.0 / 3.0 },
            { p ("A5"), 6.0, 0.25 }, { p ("B5"), 6.25, 0.25 }, { p ("C6"), 6.5, 0.25 }, { p ("D6"), 6.75, 0.25 },
            { p ("C3"), 4.0, 2.0 }, { p ("E3"), 4.0, 2.0 }, { p ("G3"), 4.0, 2.0 } }, 12.0), {});
        const auto mei = scoreToMei (result.score);

        CHECK (mei.find ("tie=\"i\"") != std::string::npos);
        CHECK (mei.find ("tie=\"t\"") != std::string::npos);
        CHECK (mei.find ("<tuplet num=\"3\" numbase=\"2\">") != std::string::npos);
        CHECK (mei.find ("<beam>") != std::string::npos);
        CHECK (mei.find ("<chord ") != std::string::npos);
        CHECK (mei.find ("<mRest ") != std::string::npos);
        CHECK (mei.find ("tstamp=") != std::string::npos);
        CHECK (mei.find ("midi.bpm=\"120\"") != std::string::npos);
        checkMei (result, "ties and tuplets");
    }

    void testMetersAndPickup()
    {
        // 4/4, then 3/4, then 4/4; the clip starts on the last beat of a bar
        auto capture = tsupport::capture ({ { p ("C4"), 3.0, 1.0 }, { p ("D4"), 4.0, 1.0 }, { p ("E4"), 8.0, 1.0 }, { p ("F4"), 11.0, 1.0 },
                                            { p ("G4"), 12.0, 1.0 } }, 16.0,
                                          { { 0.0, 4, 4 }, { 4.0, 4, 4 }, { 8.0, 3, 4 }, { 11.0, 4, 4 }, { 15.0, 4, 4 } });
        const auto result = transcribePiano (capture, {});
        const auto mei = scoreToMei (result.score);

        CHECK (mei.find ("<scoreDef meter.count=\"3\" meter.unit=\"4\"/>") != std::string::npos);
        CHECK (mei.find ("<scoreDef meter.count=\"4\" meter.unit=\"4\"/>") != std::string::npos);
        checkMei (result, "meters");

        // a pickup bar is number 0 and does not count as a full bar
        const auto pick = transcribePiano (tsupport::capture ({ { p ("G4"), 3.0, 1.0 }, { p ("C5"), 4.0, 2.0 }, { p ("E5"), 6.0, 2.0 } }, 8.0), {});
        const auto pickMei = scoreToMei (pick.score);
        CHECK (pickMei.find ("n=\"0\" metcon=\"false\"") != std::string::npos);
        checkMei (pick, "pickup");
    }

    void testNodeDescriptions()
    {
        const auto result = transcribePiano (tsupport::capture ({ { p ("Eb5"), 0.0, 0.5 }, { p ("C3"), 0.0, 4.0 },
                                                                  { p ("C4"), 1.0, 1.0 }, { p ("E4"), 1.0, 1.0 }, { p ("G4"), 1.0, 1.0 } }), {});
        const Node* note = nullptr;
        const Node* chord = nullptr;
        const Node* rest = nullptr;
        std::vector<const Node*> stack { &result.score.root() };

        while (! stack.empty())
        {
            const auto* n = stack.back();
            stack.pop_back();

            if (n->type == nodeType::note && n->prop ("oct").asInt() == 5) note = n;
            if (n->type == nodeType::chord) chord = n;
            if (n->type == nodeType::rest && ! n->prop ("measureRest").asBool()) rest = n;

            for (const auto& c : n->children)
                stack.push_back (&c);
        }

        CHECK (note != nullptr);
        CHECK (chord != nullptr);
        CHECK (rest != nullptr);

        if (note != nullptr)
        {
            const auto text = describeNode (result.score, note->id);
            CHECK (text.find ("Right hand") == 0);
            CHECK (text.find ("measure 1") != std::string::npos);
            CHECK (text.find ("voice 1") != std::string::npos);
            CHECK (text.find ("eighth note D#5") != std::string::npos || text.find ("eighth note Eb5") != std::string::npos);
        }

        if (chord != nullptr)
        {
            const auto text = describeNode (result.score, chord->id);
            CHECK (text.find ("quarter chord C4 E4 G4") != std::string::npos);

            // a click on a note inside the chord says the chord
            const auto inner = describeNode (result.score, chord->children.front().id);
            CHECK (inner.find ("chord") != std::string::npos);
        }

        if (rest != nullptr)
            CHECK (describeNode (result.score, rest->id).find ("rest") != std::string::npos);

        CHECK (describeNode (result.score, "no-such-id").empty());
    }

    void testTripletAndTieDescriptions()
    {
        const auto result = transcribePiano (tsupport::capture ({ { p ("C5"), 3.0, 2.0 },
                                                                  { p ("E5"), 5.0, 1.0 / 3.0 }, { p ("F5"), 5.0 + 1.0 / 3.0, 1.0 / 3.0 },
                                                                  { p ("G5"), 5.0 + 2.0 / 3.0, 1.0 / 3.0 } }, 8.0), {});
        bool triplet = false, tied = false;
        std::vector<const Node*> stack { &result.score.root() };

        while (! stack.empty())
        {
            const auto* n = stack.back();
            stack.pop_back();

            if (n->type == nodeType::note)
            {
                const auto text = describeNode (result.score, n->id);
                triplet = triplet || text.find ("eighth triplet note") != std::string::npos;
                tied = tied || text.find (", tied to the next note") != std::string::npos;
            }

            for (const auto& c : n->children)
                stack.push_back (&c);
        }

        CHECK (triplet);
        CHECK (tied);
    }

    void testFixtureFilesGiveValidMei()
    {
#ifdef TRANSCRIBER_FIXTURES_DIR
        const std::string dir = TRANSCRIBER_FIXTURES_DIR;

        for (const char* name : { "t23-piano-3min.mid", "t13-timing.mid", "t21-loop8.mid", "t11-piano.mid", "t22-verse.mid", "t23-drums-3min.mid" })
        {
            auto f = midireader::read (dir + "/" + name);
            testing::report (f.ok, __FILE__, __LINE__, std::string (name) + ": " + f.error);

            if (f.ok)
                checkMei (transcribePiano (f.capture, {}), name);
        }
#endif
    }

    void testRandomClipsGiveValidMei()
    {
        std::mt19937 rng (777);

        for (int clip = 0; clip < 40; ++clip)
        {
            std::vector<N> notes;
            const int count = 5 + (int) (rng() % 60);

            for (int i = 0; i < count; ++i)
            {
                const double start = (double) (rng() % 128) / 8.0 + (rng() % 5 == 0 ? (double) (rng() % 100) / 300.0 : 0.0);
                const double dur = 0.1 + (double) (rng() % 16) / 8.0;
                notes.push_back ({ 36 + (int) (rng() % 60), start, dur, 80 });
            }

            trs::ResolvedCapture rc;
            double end = 0.0;

            for (const auto& n : notes)
            {
                trs::ResolvedNote r;
                r.onPpq = n.start;
                r.offPpq = n.start + n.dur;
                r.pitch = n.pitch;
                r.velocity = n.vel;
                rc.notes.push_back (r);
                end = std::max (end, r.offPpq);
            }

            std::sort (rc.notes.begin(), rc.notes.end(), [] (const trs::ResolvedNote& x, const trs::ResolvedNote& y)
                       { return x.onPpq != y.onPpq ? x.onPpq < y.onPpq : x.pitch < y.pitch; });
            rc.lengthPpq = std::ceil (end / 4.0) * 4.0;

            for (double b = 0.0; b < rc.lengthPpq - 1.0e-9; b += 4.0)
                rc.bars.push_back ({ b, 4, 4 });

            rc.tempoMap.push_back ({ 0.0, 120.0 });
            const auto result = transcribePiano (rc, {});
            checkMei (result, "random clip " + std::to_string (clip));
        }
    }

    void testXmlEscape()
    {
        CHECK_STR (xmlEscape ("a<b>&\"c\"").c_str(), "a&lt;b&gt;&amp;&quot;c&quot;");
        CHECK_STR (xmlEscape ("plain").c_str(), "plain");
    }

    REGISTER (testBasics, "mei: basics");
    REGISTER (testAccidentalsAndKey, "mei: key signature and accidentals");
    REGISTER (testTiesTupletsBeamsAndRests, "mei: ties, tuplets, beams, chords and rests");
    REGISTER (testMetersAndPickup, "mei: meter changes and a pickup bar");
    REGISTER (testNodeDescriptions, "mei: what a click on the page says");
    REGISTER (testTripletAndTieDescriptions, "mei: triplets and ties in the click sentence");
    REGISTER (testFixtureFilesGiveValidMei, "mei: the test clips give valid MEI");
    REGISTER (testRandomClipsGiveValidMei, "mei: 40 random clips give valid MEI");
    REGISTER (testXmlEscape, "mei: xml escaping");
}
