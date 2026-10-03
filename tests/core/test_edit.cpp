// Phase 7a tests: the editing operations. Every operation is applied, checked and undone; random
// sequences of edits keep the score well formed and undo back to the start.

#include "TestSupport.h"
#include "TranscribeSupport.h"

#include "core/Edit.h"
#include "core/Notation.h"

#include <random>
#include <set>

using namespace trs;
using namespace tsupport;

#define REGISTER(fn, name) static testing::Registrar registrar_##fn (name, fn)

namespace
{
    // The events (notes, chords, rests) of the first staff, in order.
    std::vector<const Node*> events (const Score& score, int staffNumber = 1)
    {
        std::vector<const Node*> out;

        for (const auto& part : score.root().children)
            for (const auto& staff : part.children)
                if (staff.prop ("n").asInt() == staffNumber)
                    for (const auto& m : staff.children)
                        for (const auto& layer : m.children)
                            if (layer.type == nodeType::layer)
                                for (const auto& e : layer.children)
                                    out.push_back (&e);

        return out;
    }

    std::string idOf (const Score& score, size_t index, int staffNumber = 1)
    {
        const auto all = events (score, staffNumber);
        return index < all.size() ? all[index]->id : std::string();
    }

    // "S1 v1: ..." lines of the right hand only, e.g. "m1 C4/4 E4/4 R".
    std::string right (const Score& score)
    {
        const auto text = dumpScore (score);
        std::string out;
        size_t at = 0;

        while (at < text.size())
        {
            auto end = text.find ('\n', at);

            if (end == std::string::npos)
                end = text.size();

            auto line = text.substr (at, end - at);
            at = end + 1;

            if (line.rfind ("m", 0) == 0)
                out += (out.empty() ? "" : " | ") + line.substr (0, line.find (' '));
            else if (line.find ("S1 ") != std::string::npos)
                out += " " + line.substr (line.find (':') + 2);
        }

        return out;
    }

    Json req (const char* op, const std::string& id)
    {
        Json j = Json::object();
        j.set ("op", op);
        j.set ("id", id);
        return j;
    }

    Json req (const char* op, const std::string& id, const char* key, int value)
    {
        auto j = req (op, id);
        j.set (key, value);
        return j;
    }

    ResolvedCapture takeOf (const std::vector<N>& notes, double length, int beatsPerBar = 4)
    {
        ResolvedCapture rc;

        for (const auto& n : notes)
        {
            ResolvedNote r;
            r.onPpq = n.start;
            r.offPpq = n.start + n.dur;
            r.pitch = n.pitch;
            r.velocity = n.vel;
            rc.notes.push_back (r);
        }

        rc.lengthPpq = length;

        for (double b = 0.0; b < length - 1.0e-9; b += beatsPerBar)
            rc.bars.push_back ({ b, beatsPerBar, 4 });

        rc.tempoMap.push_back ({ 0.0, 120.0 });
        return rc;
    }

    // Every voice of every measure fills the measure exactly, in order, with sensible values.
    std::string problems (const Score& score)
    {
        if (const auto v = score.validate(); ! v.empty())
            return v;

        for (const auto& part : score.root().children)
            for (const auto& staff : part.children)
                for (const auto& m : staff.children)
                    for (const auto& layer : m.children)
                    {
                        if (layer.type != nodeType::layer)
                            continue;

                        if (layer.prop ("n").asInt() < 1)
                            return "a voice number below 1";

                        if (layer.prop ("n").asInt() > 1 && layer.children.size() == 1 && layer.children.front().prop ("measureRest").asBool())
                            return "an empty voice above the first";

                        int64_t at = 0;
                        std::map<std::string, int> beamed;

                        for (const auto& e : layer.children)
                        {
                            if (e.prop ("onset").asInt() != at)
                                return "gap or overlap in measure " + std::to_string (m.prop ("n").asInt()) + " at " + std::to_string (at);

                            at += e.prop ("ticks").asInt();

                            if (! e.prop ("beam").asString().empty())
                                ++beamed[e.prop ("beam").asString()];

                            if (e.type == nodeType::chord)
                            {
                                std::set<int64_t> pitches;

                                for (const auto& n : e.children)
                                {
                                    if (! pitches.insert (n.prop ("pitch").asInt()).second)
                                        return "a pitch twice in a chord";

                                    if (n.prop ("onset").asInt() != e.prop ("onset").asInt())
                                        return "a chord note with another onset";
                                }

                                if (e.children.size() < 2)
                                    return "a chord of one note";
                            }

                            if (e.type == nodeType::note && staff.prop ("kind").asString() != "tab" && staff.prop ("kind").asString() != "perc")
                            {
                                const char* names = "CDEFGAB";
                                const auto step = e.prop ("step").asString();

                                if (step.size() != 1 || std::string (names).find (step[0]) == std::string::npos)
                                    return "a note without a letter";

                                if (pitchOf (step[0], (int) e.prop ("alter").asInt(), (int) e.prop ("oct").asInt()) != e.prop ("pitch").asInt())
                                    return "pitch and spelling disagree";
                            }
                        }

                        if (at != m.prop ("ticks").asInt())
                            return "measure " + std::to_string (m.prop ("n").asInt()) + " is " + std::to_string (at) + " long, not " + std::to_string (m.prop ("ticks").asInt());

                        for (const auto& [id, count] : beamed)
                            if (count < 2)
                                return "a beam of one note";
                    }

        return {};
    }

    // Applies an edit and checks that undo and redo give back the score exactly.
    struct Session
    {
        Score score;
        UndoManager undo { score };

        explicit Session (Score s) : score (std::move (s)) { undo.rebind (score); }

        EditResult run (const Json& request)
        {
            const Score before = score;
            auto result = performEdit (score, undo, request);

            if (! result.ok)
            {
                CHECK (score == before);   // a refused edit changes nothing
                return result;
            }

            CHECK (score.validate().empty());
            const Score after = score;
            CHECK (undo.undo());
            CHECK (score == before);
            CHECK (undo.redo());
            CHECK (score == after);
            return result;
        }
    };

    Score pianoScore (std::vector<N> notes, double length, int tonic = -1, bool minor = false, int beatsPerBar = 4)
    {
        TranscriptionSettings settings;
        settings.keyTonic = tonic;
        settings.keyMinor = minor;
        return transcribePiano (takeOf (notes, length, beatsPerBar), settings).score;
    }
}

//==============================================================================
static void testPitch()
{
    Session s (pianoScore ({ { p ("C4"), 0, 1 }, { p ("E4"), 1, 1 }, { p ("G4"), 2, 1 }, { p ("C5"), 3, 1 } }, 4.0, 0));
    CHECK_STR (right (s.score).c_str(), "m1 C4/4 E4/4 G4/4 C5/4");

    auto r = s.run (req ("pitch", idOf (s.score, 1), "semitones", 1));
    CHECK (r.ok);
    CHECK_STR (right (s.score).c_str(), "m1 C4/4 F4/4 G4/4 C5/4");
    CHECK (s.undo.undo());

    // a chromatic note in C major is written with a sharp
    r = s.run (req ("pitch", idOf (s.score, 0), "semitones", 1));
    CHECK (r.ok);
    CHECK_STR (right (s.score).c_str(), "m1 C#4!/4 E4/4 G4/4 C5/4");
    CHECK_STR (events (s.score)[0]->prop ("accid").asString().c_str(), "s");
    CHECK (s.undo.undo());

    // down: the sharp is kept for a sharp key, a flat for a flat key
    r = s.run (req ("pitch", idOf (s.score, 2), "semitones", -1));
    CHECK_STR (right (s.score).c_str(), "m1 C4/4 E4/4 F#4!/4 C5/4");
    CHECK (s.undo.undo());

    // an octave keeps the spelling
    r = s.run (req ("pitch", idOf (s.score, 3), "semitones", -12));
    CHECK_STR (right (s.score).c_str(), "m1 C4/4 E4/4 G4/4 C4/4");
    CHECK (s.undo.undo());

    // the range of the piano
    Session top (pianoScore ({ { p ("C8"), 0, 1 } }, 4.0, 0));
    CHECK (! top.run (req ("pitch", idOf (top.score, 0), "semitones", 1)).ok);
    CHECK (! top.run (req ("pitch", idOf (top.score, 0), "semitones", 12)).ok);
    CHECK (top.run (req ("pitch", idOf (top.score, 0), "semitones", -1)).ok);

    // flat key: a note of the scale is spelled as the key signature says
    Session f (pianoScore ({ { p ("F4"), 0, 1 }, { p ("A4"), 1, 1 }, { p ("C5"), 2, 1 }, { p ("F5"), 3, 1 } }, 4.0, 5));
    CHECK_STR (right (f.score).c_str(), "m1 F4/4 A4/4 C5/4 F5/4");
    f.run (req ("pitch", idOf (f.score, 1), "semitones", 1));
    CHECK_STR (right (f.score).c_str(), "m1 F4/4 Bb4/4 C5/4 F5/4");
    CHECK (events (f.score)[1]->prop ("accid").asString().empty());   // the key signature has it

    // the same in C major: A sharp
    Session c (pianoScore ({ { p ("A4"), 0, 1 } }, 4.0, 0));
    c.run (req ("pitch", idOf (c.score, 0), "semitones", 1));
    CHECK_STR (right (c.score).c_str(), "m1 A#4!/4 r/4 r/2");

    // a rest has no pitch
    Session rest (pianoScore ({ { p ("C4"), 0, 1 } }, 4.0, 0));
    CHECK (! rest.run (req ("pitch", idOf (rest.score, 1), "semitones", 1)).ok);
}
REGISTER (testPitch, "edit: pitch up, down, octave, range, spelling by key");

static void testAccidentals()
{
    // an accidental holds for the rest of the measure and is shown again where it changes
    Session s (pianoScore ({ { p ("C4"), 0, 1 }, { p ("C4"), 1, 1 }, { p ("C4"), 2, 1 }, { p ("C4"), 3, 1 } }, 4.0, 0));
    s.run (req ("pitch", idOf (s.score, 1), "semitones", 1));
    const auto e = events (s.score);
    CHECK (e[0]->prop ("accid").asString().empty());
    CHECK_STR (e[1]->prop ("accid").asString().c_str(), "s");
    CHECK_STR (e[2]->prop ("accid").asString().c_str(), "n");   // back to natural
    CHECK (e[3]->prop ("accid").asString().empty());

    // and undone, they are gone again
    CHECK (s.undo.undo());
    for (const auto* n : events (s.score))
        CHECK (n->prop ("accid").asString().empty());
}
REGISTER (testAccidentals, "edit: accidentals are worked out again");

static void testDelete()
{
    Session s (pianoScore ({ { p ("C4"), 0, 1 }, { p ("E4"), 1, 1 }, { p ("G4"), 2, 0.5 }, { p ("A4"), 2.5, 0.5 }, { p ("C5"), 3, 1 } }, 4.0, 0));
    CHECK_STR (right (s.score).c_str(), "m1 C4/4 E4/4 ( G4/8 A4/8 ) C5/4");

    auto r = s.run (req ("delete", idOf (s.score, 1)));
    CHECK (r.ok);
    CHECK_STR (right (s.score).c_str(), "m1 C4/4 r/4 ( G4/8 A4/8 ) C5/4");
    CHECK_STR (r.select.c_str(), idOf (s.score, 1).c_str());

    // the notes next to a rest join it: all of them gone gives a measure rest
    CHECK (s.run (req ("delete", idOf (s.score, 0))).ok);
    CHECK_STR (right (s.score).c_str(), "m1 r/2 ( G4/8 A4/8 ) C5/4");

    for (int guard = 0; guard < 10; ++guard)
    {
        const auto all = events (s.score);
        const auto note = std::find_if (all.begin(), all.end(), [] (const Node* n) { return n->type != nodeType::rest; });

        if (note == all.end())
            break;

        CHECK (s.run (req ("delete", (*note)->id)).ok);
    }

    CHECK_STR (right (s.score).c_str(), "m1 R");
    CHECK_EQ (events (s.score).size(), 1u);   // one measure rest
    CHECK (events (s.score).front()->prop ("measureRest").asBool());

    // a rest cannot be deleted
    CHECK (! s.run (req ("delete", idOf (s.score, 0))).ok);
    CHECK (! s.run (req ("delete", "nonsense")).ok);
}
REGISTER (testDelete, "edit: delete makes a rest and joins rests");

static void testDuration()
{
    Session s (pianoScore ({ { p ("C4"), 0, 1 }, { p ("E4"), 1, 1 }, { p ("G4"), 2, 1 }, { p ("C5"), 3, 1 } }, 4.0, 0));

    // shorter: the rest of the value becomes a rest
    auto r = s.run (req ("duration", idOf (s.score, 0), "dur", 8));
    CHECK (r.ok);
    CHECK_STR (right (s.score).c_str(), "m1 C4/8 r/8 E4/4 G4/4 C5/4");
    CHECK_STR (r.select.c_str(), idOf (s.score, 0).c_str());

    // longer: the rest is taken
    r = s.run (req ("duration", idOf (s.score, 0), "dur", 4));
    CHECK_STR (right (s.score).c_str(), "m1 C4/4 E4/4 G4/4 C5/4");
    CHECK (s.undo.undo());
    CHECK (s.undo.undo());

    // longer than the space: the notes after it are taken out
    r = s.run (req ("duration", idOf (s.score, 0), "dur", 2));
    CHECK (r.ok);
    CHECK_STR (right (s.score).c_str(), "m1 C4/2 G4/4 C5/4");
    CHECK (r.message.find ("taken out") != std::string::npos);
    CHECK (s.undo.undo());

    // the whole bar
    r = s.run (req ("duration", idOf (s.score, 0), "dur", 1));
    CHECK_STR (right (s.score).c_str(), "m1 C4/1");
    CHECK (s.undo.undo());

    // dotted
    auto dotted = req ("duration", idOf (s.score, 0), "dur", 4);
    dotted.set ("dots", 1);
    r = s.run (dotted);
    CHECK_STR (right (s.score).c_str(), "m1 C4/4. r/8 G4/4 C5/4");   // E4 was covered; the eighth that is left is a rest
    s.undo.undo();

    // it does not fit
    CHECK (! s.run (req ("duration", idOf (s.score, 3), "dur", 1)).ok);
    CHECK (! s.run (req ("duration", idOf (s.score, 0), "dur", 3)).ok);   // there is no such value
    CHECK (! s.run (req ("duration", idOf (s.score, 0), "dur", 4)).ok);   // it already is that
}
REGISTER (testDuration, "edit: length change takes and gives room");

static void testDot()
{
    Session s (pianoScore ({ { p ("C4"), 0, 1 }, { p ("E4"), 1, 1 } }, 4.0, 0));
    CHECK_STR (right (s.score).c_str(), "m1 C4/4 E4/4 r/2");
    s.run (req ("dot", idOf (s.score, 0)));
    CHECK_STR (right (s.score).c_str(), "m1 C4/4. r/8 r/2");
    CHECK (s.undo.undo());
    s.run (req ("dot", idOf (s.score, 1)));
    CHECK_STR (right (s.score).c_str(), "m1 C4/4 E4/4. r/8 r/4");
}
REGISTER (testDot, "edit: dot");

static void testTriplets()
{
    // three notes in the time of two: their lengths cannot be changed, but their pitch can
    Session s (pianoScore ({ { p ("C4"), 0, 1.0 / 3 }, { p ("D4"), 1.0 / 3, 1.0 / 3 }, { p ("E4"), 2.0 / 3, 1.0 / 3 }, { p ("G4"), 1, 3 } }, 4.0, 0));
    CHECK (events (s.score)[0]->has ("tuplet"));
    CHECK (! s.run (req ("duration", idOf (s.score, 0), "dur", 4)).ok);
    CHECK (s.run (req ("pitch", idOf (s.score, 1), "semitones", 2)).ok);
    CHECK (s.run (req ("delete", idOf (s.score, 1))).ok);   // becomes a triplet rest
    CHECK (events (s.score)[1]->type == nodeType::rest);
    CHECK (events (s.score)[1]->has ("tuplet"));
}
REGISTER (testTriplets, "edit: triplets keep their length");

static void testEnterNote()
{
    Session s (pianoScore ({ { p ("C4"), 0, 1 } }, 8.0, 0));
    CHECK_STR (right (s.score).c_str(), "m1 C4/4 r/4 r/2 | m2 R");

    // the rest after C4: a letter makes a note in the octave nearest the one before
    auto id = idOf (s.score, 1);
    auto j = req ("letter", id);
    j.set ("letter", "G");
    auto r = s.run (j);
    CHECK (r.ok);
    CHECK_STR (right (s.score).c_str(), "m1 C4/4 G3/4 r/2 | m2 R");   // G3 is nearer to C4 than G4
    CHECK_STR (r.select.c_str(), id.c_str());

    // a whole-bar rest gives a note of the bar's value, near the music before
    id = idOf (s.score, 3);
    j = req ("letter", id);
    j.set ("letter", "E");
    r = s.run (j);
    CHECK (r.ok);
    CHECK_STR (right (s.score).c_str(), "m1 C4/4 G3/4 r/2 | m2 E3/1");

    // on a note the letter changes its pitch
    j = req ("letter", idOf (s.score, 0));
    j.set ("letter", "A");
    CHECK (s.run (j).ok);
    CHECK_STR (right (s.score).c_str(), "m1 A3/4 G3/4 r/2 | m2 E3/1");

    // with an accidental
    j = req ("letter", idOf (s.score, 1));
    j.set ("letter", "B");
    j.set ("alter", -1);
    CHECK (s.run (j).ok);
    CHECK_STR (right (s.score).c_str(), "m1 A3/4 Bb3!/4 r/2 | m2 E3/1");

    // not a letter
    j = req ("letter", idOf (s.score, 1));
    j.set ("letter", "H");
    CHECK (! s.run (j).ok);

    // a 3/4 bar of rest: the note is a dotted half
    ResolvedCapture rc;
    ResolvedNote n;
    n.onPpq = 0; n.offPpq = 3; n.pitch = 60;
    rc.notes.push_back (n);
    rc.lengthPpq = 6.0;
    rc.bars = { { 0.0, 3, 4 }, { 3.0, 3, 4 } };
    rc.tempoMap.push_back ({ 0.0, 120.0 });
    TranscriptionSettings st;
    st.keyTonic = 0;
    Session three (transcribePiano (rc, st).score);
    j = req ("letter", idOf (three.score, 1));
    j.set ("letter", "C");
    CHECK (three.run (j).ok);
    CHECK_STR (right (three.score).c_str(), "m1 C4/2. | m2 C4/2.");
}
REGISTER (testEnterNote, "edit: enter a note on a rest, change the letter");

static void testChords()
{
    Session s (pianoScore ({ { p ("C4"), 0, 1 }, { p ("G4"), 1, 1 } }, 4.0, 0));

    auto j = req ("interval", idOf (s.score, 0));
    j.set ("interval", 3);
    auto r = s.run (j);
    CHECK (r.ok);
    CHECK_STR (right (s.score).c_str(), "m1 [C4 E4]/4 G4/4 r/2");
    CHECK_STR (r.select.c_str(), idOf (s.score, 0).c_str());

    // another one on the chord: a fifth above the top note
    j = req ("interval", idOf (s.score, 0));
    j.set ("interval", 5);
    CHECK (s.run (j).ok);
    CHECK_STR (right (s.score).c_str(), "m1 [C4 E4 B4]/4 G4/4 r/2");   // a fifth above E4
    CHECK (s.undo.undo());

    // chord: the pitch moves all of them
    CHECK (s.run (req ("pitch", idOf (s.score, 0), "semitones", 1)).ok);
    CHECK_STR (right (s.score).c_str(), "m1 [C#4! F4]/4 G4/4 r/2");
    CHECK (s.undo.undo());

    // one note of a chord: select it by its own id
    const auto chord = events (s.score)[0];
    CHECK (chord->type == nodeType::chord);
    const auto upper = chord->children[1].id;
    CHECK (s.run (req ("pitch", upper, "semitones", 2)).ok);
    CHECK_STR (right (s.score).c_str(), "m1 [C4 F#4!]/4 G4/4 r/2");
    CHECK (s.undo.undo());

    // moving one note onto another is refused
    CHECK (! s.run (req ("pitch", upper, "semitones", -4)).ok);

    // taking a note out of a two-note chord leaves a plain note
    auto r2 = s.run (req ("delete", upper));
    CHECK (r2.ok);
    CHECK_STR (right (s.score).c_str(), "m1 C4/4 G4/4 r/2");
    CHECK (events (s.score)[0]->type == nodeType::note);

    // the whole chord becomes a rest
    CHECK (s.undo.undo());
    CHECK (s.run (req ("delete", idOf (s.score, 0))).ok);
    CHECK_STR (right (s.score).c_str(), "m1 r/4 G4/4 r/2");

    // a rest cannot get an interval
    j = req ("interval", idOf (s.score, 0));
    j.set ("interval", 3);
    CHECK (! s.run (j).ok);
    j = req ("interval", idOf (s.score, 1));
    j.set ("interval", 9);
    CHECK (! s.run (j).ok);
}
REGISTER (testChords, "edit: chords, one note of a chord");

static void testTies()
{
    Session s (pianoScore ({ { p ("C4"), 0, 1 }, { p ("C4"), 1, 1 }, { p ("D4"), 2, 1 }, { p ("D4"), 3, 1 }, { p ("D4"), 4, 1 } }, 8.0, 0));

    auto r = s.run (req ("tie", idOf (s.score, 0)));
    CHECK (r.ok);
    CHECK_STR (events (s.score)[0]->prop ("tie").asString().c_str(), "i");
    CHECK_STR (events (s.score)[1]->prop ("tie").asString().c_str(), "t");
    CHECK (events (s.score)[1]->prop ("accid").asString().empty());

    // a tie to a different pitch is refused
    CHECK (! s.run (req ("tie", idOf (s.score, 1))).ok);

    // across the bar line
    CHECK (s.run (req ("tie", idOf (s.score, 3))).ok);
    CHECK_STR (events (s.score)[3]->prop ("tie").asString().c_str(), "i");
    CHECK_STR (events (s.score)[4]->prop ("tie").asString().c_str(), "t");

    // a note followed by a rest has nothing to tie to
    CHECK (! s.run (req ("tie", idOf (s.score, 4))).ok);
}
REGISTER (testTies, "edit: tie");

static void testTieKept()
{
    // three notes of one pitch tied in a row
    Session s (pianoScore ({ { p ("C4"), 0, 1 }, { p ("C4"), 1, 1 }, { p ("C4"), 2, 1 } }, 4.0, 0));
    CHECK (s.run (req ("tie", idOf (s.score, 0))).ok);
    CHECK (s.run (req ("tie", idOf (s.score, 1))).ok);
    CHECK_STR (events (s.score)[1]->prop ("tie").asString().c_str(), "m");

    // taking the middle one out removes the ties that led to and from it
    CHECK (s.run (req ("delete", idOf (s.score, 1))).ok);
    CHECK (events (s.score)[0]->prop ("tie").asString().empty());
    CHECK (events (s.score)[2]->prop ("tie").asString().empty());

    // moving a tied note takes its tie away
    CHECK (s.undo.undo());
    CHECK (s.run (req ("pitch", idOf (s.score, 1), "semitones", 2)).ok);
    CHECK (events (s.score)[0]->prop ("tie").asString().empty());
    CHECK (events (s.score)[2]->prop ("tie").asString().empty());
}
REGISTER (testTieKept, "edit: ties follow the notes");

static void testBeams()
{
    Session s (pianoScore ({ { p ("C4"), 0, 0.5 }, { p ("D4"), 0.5, 0.5 }, { p ("E4"), 1, 0.5 }, { p ("F4"), 1.5, 0.5 } }, 4.0, 0));
    CHECK_STR (right (s.score).c_str(), "m1 ( C4/8 D4/8 ) ( E4/8 F4/8 ) r/2");
    s.run (req ("delete", idOf (s.score, 1)));
    CHECK_STR (right (s.score).c_str(), "m1 C4/8 r/8 ( E4/8 F4/8 ) r/2");   // a lone eighth has no beam
    CHECK (s.undo.undo());
    s.run (req ("duration", idOf (s.score, 1), "dur", 4));
    CHECK_STR (right (s.score).c_str(), "m1 C4/8 D4/4 F4/8 r/2");
}
REGISTER (testBeams, "edit: beams are made again");

static void testUndo()
{
    Session s (pianoScore ({ { p ("C4"), 0, 1 }, { p ("E4"), 1, 1 } }, 4.0, 0));
    const Score original = s.score;

    CHECK (! performEdit (s.score, s.undo, [] { Json j = Json::object(); j.set ("op", "undo"); return j; }()).ok);

    CHECK (performEdit (s.score, s.undo, req ("pitch", idOf (s.score, 0), "semitones", 1)).ok);
    CHECK (performEdit (s.score, s.undo, req ("pitch", idOf (s.score, 1), "semitones", 1)).ok);
    CHECK_STR (s.undo.undoName().c_str(), "Change pitch");

    Json u = Json::object();
    u.set ("op", "undo");
    Json redo = Json::object();
    redo.set ("op", "redo");
    CHECK (performEdit (s.score, s.undo, u).ok);
    CHECK (performEdit (s.score, s.undo, u).ok);
    CHECK (s.score == original);
    CHECK (! performEdit (s.score, s.undo, u).ok);
    CHECK (performEdit (s.score, s.undo, redo).ok);
    CHECK_STR (right (s.score).c_str(), "m1 C#4!/4 E4/4 r/2");
}
REGISTER (testUndo, "edit: undo and redo as operations");

static void testBlocked()
{
    // piano, guitar, bass and drum scores are edited; an empty score is not
    TranscriptionSettings settings;
    ResolvedCapture rc = takeOf ({ { 40, 0, 1 } }, 4.0);
    auto guitar = transcribeFretted (rc, settings, InstrumentType::guitar).score;
    auto bass = transcribeFretted (takeOf ({ { 28, 0, 1 } }, 4.0), settings, InstrumentType::bass).score;
    CHECK (editBlocker (guitar).empty());
    CHECK (editBlocker (bass).empty());

    auto drums = transcribeDrums (takeOf ({ { 38, 0, 0.1 } }, 4.0), settings, drumPreset ("gm")).score;
    CHECK (editBlocker (drums).empty());

    Score empty;
    CHECK (! editBlocker (empty).empty());
}
REGISTER (testBlocked, "edit: every instrument can be edited, an empty score cannot");

//==============================================================================
// Phase 7e: guitar and bass. The notation staff is edited and the tab staff follows.

#include "core/Instruments.h"
#include "core/Mei.h"
#include "XmlCheck.h"

namespace
{
    Score fretted (std::vector<N> notes, double length, InstrumentType type = InstrumentType::guitar, int beatsPerBar = 4)
    {
        TranscriptionSettings settings;
        return transcribeFretted (takeOf (notes, length, beatsPerBar), settings, type).score;
    }

    // "6:0 5:1 r [3:0 2:0]": the tab events of a score, as course:fret.
    std::string tabLine (const Score& score)
    {
        std::string out;

        for (const auto* e : events (score, 2))
        {
            auto one = [] (const Node& n) { return std::to_string (n.prop ("course").asInt()) + ":" + std::to_string (n.prop ("fret").asInt()); };
            std::string text;

            if (e->type == nodeType::rest)
            {
                text = "r";
            }
            else if (e->type == nodeType::note)
            {
                text = one (*e);
            }
            else
            {
                text = "[";

                for (const auto& n : e->children)
                    text += (text.size() > 1 ? " " : "") + one (n);

                text += "]";
            }

            out += (out.empty() ? "" : " ") + text;
        }

        return out;
    }

    // The tab staff says what the notation staff says: the same events, every tab note the pitch of its notation note,
    // on a string that can play it, no string used twice in a chord.
    std::string tabProblems (const Score& score)
    {
        const Node* part = nullptr;

        for (const auto& c : score.root().children)
            if (c.type == nodeType::part)
                part = &c;

        if (part == nullptr)
            return {};

        const Node* notation = nullptr;
        const Node* tab = nullptr;

        for (const auto& s : part->children)
        {
            if (s.prop ("kind").asString() == "tab")
                tab = &s;
            else if (notation == nullptr)
                notation = &s;
        }

        if (tab == nullptr || notation == nullptr)
            return {};

        const int strings = (int) tab->prop ("strings").asInt (6);
        const auto open = openStrings (strings == 4 ? InstrumentType::bass : InstrumentType::guitar);

        if (tab->children.size() != notation->children.size())
            return "the tab has another number of measures";

        auto notesOf = [] (const Node& e)
        {
            std::vector<const Node*> notes;

            if (e.type == nodeType::note)
                notes.push_back (&e);
            else if (e.type == nodeType::chord)
                for (const auto& n : e.children)
                    notes.push_back (&n);

            return notes;
        };

        for (size_t i = 0; i < notation->children.size(); ++i)
        {
            std::vector<const Node*> a, b;

            for (const auto& l : notation->children[i].children) if (l.type == nodeType::layer) a.push_back (&l);
            for (const auto& l : tab->children[i].children) if (l.type == nodeType::layer) b.push_back (&l);

            if (a.size() != b.size())
                return "measure " + std::to_string (i + 1) + ": another number of voices in the tab";

            for (size_t l = 0; l < a.size(); ++l)
            {
                if (a[l]->children.size() != b[l]->children.size())
                    return "measure " + std::to_string (i + 1) + ": another number of events in the tab";

                for (size_t k = 0; k < a[l]->children.size(); ++k)
                {
                    const auto& x = a[l]->children[k];
                    const auto& y = b[l]->children[k];

                    if (x.type != y.type)
                        return "measure " + std::to_string (i + 1) + ": a note and a chord do not match";

                    for (const char* key : { "onset", "ticks", "dur", "dots", "tuplet", "beam", "measureRest" })
                        if (! (x.prop (key) == y.prop (key)))
                            return std::string ("measure ") + std::to_string (i + 1) + ": the tab differs in " + key;

                    auto xn = notesOf (x), yn = notesOf (y);

                    if (xn.size() != yn.size())
                        return "measure " + std::to_string (i + 1) + ": the tab has another number of notes";

                    std::set<int64_t> courses;

                    for (size_t j = 0; j < xn.size(); ++j)
                    {
                        if (! (xn[j]->prop ("pitch") == yn[j]->prop ("pitch")))
                            return "measure " + std::to_string (i + 1) + ": the pitch of a tab note differs";

                        if (! (xn[j]->prop ("tie") == yn[j]->prop ("tie")))
                            return "measure " + std::to_string (i + 1) + ": the tie of a tab note differs";

                        const auto string = strings - (int) yn[j]->prop ("course").asInt();
                        const auto fret = (int) yn[j]->prop ("fret").asInt();

                        if (string < 0 || string >= strings || fret < 0 || fret > 22 || fret != (int) yn[j]->prop ("pitch").asInt() - open[(size_t) string])
                            return "measure " + std::to_string (i + 1) + ": a tab note is on a string that cannot play it";

                        if (! courses.insert (yn[j]->prop ("course").asInt()).second)
                            return "measure " + std::to_string (i + 1) + ": two notes on one string";

                        if (yn[j]->has ("step") || yn[j]->has ("accid") || yn[j]->has ("dyn") || yn[j]->has ("artic"))
                            return "a tab note has notation properties";
                    }
                }
            }
        }

        return {};
    }
}


static Json stringReq (const std::string& id, const char* direction)
{
    auto j = req ("string", id);
    j.set ("dir", direction);
    return j;
}

static void testGuitarEdits()
{
    Session s (fretted ({ { p ("E2"), 0, 1 }, { p ("A2"), 1, 1 }, { p ("D3"), 2, 1 }, { p ("G3"), 3, 1 }, { p ("B3"), 3, 1 } }, 4.0));
    CHECK_STR (tabLine (s.score).c_str(), "6:0 5:0 4:0 [3:0 2:0]");
    CHECK_STR (tabProblems (s.score).c_str(), "");

    // the tab nodes have the ids of their notation nodes plus "-t" from the start, so they do not change with an edit or its undo
    const auto tabIdBefore = idOf (s.score, 1, 2);
    CHECK_STR (tabIdBefore.c_str(), (idOf (s.score, 1) + "-t").c_str());
    CHECK_STR (events (s.score, 2)[3]->children[0].id.c_str(), (events (s.score, 1)[3]->children[0].id + "-t").c_str());
    CHECK (s.run (req ("pitch", idOf (s.score, 1), "semitones", 1)).ok);
    CHECK_STR (idOf (s.score, 1, 2).c_str(), tabIdBefore.c_str());
    CHECK (s.undo.undo());
    CHECK_STR (idOf (s.score, 1, 2).c_str(), tabIdBefore.c_str());
    CHECK (s.undo.redo());
    CHECK (s.undo.undo());

    // a pitch change in the notation moves the note in the tab
    CHECK (s.run (req ("pitch", idOf (s.score, 1), "semitones", 1)).ok);
    CHECK_STR (tabLine (s.score).c_str(), "6:0 5:1 4:0 [3:0 2:0]");
    CHECK_STR (right (s.score).c_str(), "m1 E2/4 A#2!/4 D3/4 [G3 B3]/4");

    // the same edit from a click in the tab: it acts on the note of the notation, and the tab note stays selected
    const auto tabNote = idOf (s.score, 1, 2);
    CHECK_STR (tabNote.c_str(), (idOf (s.score, 1) + "-t").c_str());
    const auto r = s.run (req ("pitch", tabNote, "semitones", -1));
    CHECK (r.ok);
    CHECK_STR (tabLine (s.score).c_str(), "6:0 5:0 4:0 [3:0 2:0]");

    // below the lowest string, and a chord that cannot be played: refused, nothing changes
    auto low = s.run (req ("pitch", idOf (s.score, 0), "semitones", -1));
    CHECK (! low.ok);
    CHECK (low.message.find ("cannot be played") != std::string::npos);
    CHECK (! s.run (req ("interval", idOf (s.score, 0), "interval", 3)).ok);   // E2 and G2 are both on the lowest string

    // a chord is made: the old note keeps its string
    CHECK (s.run (req ("interval", idOf (s.score, 1), "interval", 3)).ok);
    CHECK_STR (tabLine (s.score).c_str(), "6:0 [5:0 6:8] 4:0 [3:0 2:0]");
    CHECK_STR (right (s.score).c_str(), "m1 E2/4 [A2 C3]/4 D3/4 [G3 B3]/4");

    // a note becomes a rest in both staves; its length too
    CHECK (s.run (req ("delete", idOf (s.score, 2, 2))).ok);
    CHECK_STR (tabLine (s.score).c_str(), "6:0 [5:0 6:8] r [3:0 2:0]");
    CHECK (s.run (req ("duration", idOf (s.score, 0, 2), "dur", 8)).ok);
    CHECK_STR (right (s.score).c_str(), "m1 E2/8 r/8 [A2 C3]/4 r/4 [G3 B3]/4");
    CHECK_STR (tabLine (s.score).c_str(), "6:0 r [5:0 6:8] r [3:0 2:0]");
    CHECK_STR (tabProblems (s.score).c_str(), "");
    CHECK_STR (problems (s.score).c_str(), "");

    // one voice only
    CHECK (! s.run (req ("voice", idOf (s.score, 0), "voice", 2)).ok);
}
REGISTER (testGuitarEdits, "edit: guitar notes, the tab follows");

static void testStringMoves()
{
    Session s (fretted ({ { p ("E2"), 0, 1 }, { p ("A2"), 1, 1 }, { p ("D3"), 2, 1 }, { p ("G3"), 3, 1 }, { p ("B3"), 3, 1 } }, 4.0));

    // the notation staff has no strings; the first note has no lower and no higher string it could use
    CHECK (! s.run (stringReq (idOf (s.score, 1), "down")).ok);
    CHECK (! s.run (stringReq (idOf (s.score, 0, 2), "down")).ok);   // no lower string
    CHECK (! s.run (stringReq (idOf (s.score, 0, 2), "up")).ok);     // E2 does not exist on the A string

    // A2 from the A string to the low E string, fret 5; the notation is the same
    const auto before = right (s.score);
    auto moved = s.run (stringReq (idOf (s.score, 1, 2), "down"));
    CHECK (moved.ok);
    CHECK_STR (moved.select.c_str(), idOf (s.score, 1, 2).c_str());
    CHECK_STR (tabLine (s.score).c_str(), "6:0 6:5 4:0 [3:0 2:0]");
    CHECK_STR (right (s.score).c_str(), before.c_str());

    // and back up
    CHECK (s.run (stringReq (idOf (s.score, 1, 2), "up")).ok);
    CHECK_STR (tabLine (s.score).c_str(), "6:0 5:0 4:0 [3:0 2:0]");
    CHECK (s.run (stringReq (idOf (s.score, 1, 2), "down")).ok);

    // a chord: click one note of it; the other note's string is taken
    const auto chordId = events (s.score, 2)[3]->id;
    const auto lowNote = events (s.score, 2)[3]->children[0].id;
    const auto highNote = events (s.score, 2)[3]->children[1].id;
    CHECK (! s.run (stringReq (chordId, "down")).ok);        // the chord itself
    CHECK (! s.run (stringReq (lowNote, "up")).ok);          // G3 up: the B string is used
    CHECK (s.run (stringReq (lowNote, "down")).ok);
    CHECK_STR (tabLine (s.score).c_str(), "6:0 6:5 4:0 [4:5 2:0]");

    // other edits leave the notes that did not change where they are; the selected tab note stays selected (its id is renewed)
    const auto oldTabId = idOf (s.score, 2, 2);
    const auto edited = s.run (req ("pitch", oldTabId, "semitones", 1));
    CHECK (edited.ok);
    CHECK_STR (edited.select.c_str(), idOf (s.score, 2, 2).c_str());
    CHECK_STR (edited.select.c_str(), (idOf (s.score, 2) + "-t").c_str());
    CHECK_STR (tabLine (s.score).c_str(), "6:0 6:5 5:6 [4:5 2:0]");   // D#3 goes near the hand (A string, fret 6), not to the open-position D string

    // a note of the chord changes, the other one stays on its string
    const auto raised = s.run (req ("pitch", events (s.score, 2)[3]->children[1].id, "semitones", 1));
    CHECK (raised.ok);
    CHECK_STR (tabLine (s.score).c_str(), "6:0 6:5 5:6 [4:5 3:5]");   // B3 becomes C4: fret 5 on the G string, next to G3 on the D string
    CHECK_STR (tabProblems (s.score).c_str(), "");
}
REGISTER (testStringMoves, "edit: moving a tab note to another string");

static void testBassEdits()
{
    Session s (fretted ({ { 28, 0, 1 }, { 33, 1, 1 }, { 38, 2, 1 }, { 43, 3, 1 } }, 4.0, InstrumentType::bass));
    CHECK_STR (tabLine (s.score).c_str(), "4:0 3:0 2:0 1:0");
    CHECK (s.run (req ("pitch", idOf (s.score, 0), "semitones", 1)).ok);
    CHECK_STR (tabLine (s.score).c_str(), "4:1 3:0 2:0 1:0");
    CHECK (! s.run (req ("pitch", idOf (s.score, 0), "semitones", -2)).ok);   // below the E string
    CHECK (s.run (stringReq (idOf (s.score, 1, 2), "down")).ok);
    CHECK_STR (tabLine (s.score).c_str(), "4:1 4:5 2:0 1:0");
    CHECK (! s.run (stringReq (idOf (s.score, 0, 2), "up")).ok);               // A#1 is below the A string
    CHECK_STR (tabProblems (s.score).c_str(), "");
}
REGISTER (testBassEdits, "edit: bass notes and strings");

static void testFrettedMarksKeyAndMei()
{
    Session s (fretted ({ { p ("E2"), 0, 1 }, { p ("A2"), 1, 1 }, { p ("D3"), 2, 1 }, { p ("G3"), 3, 1 } }, 4.0));
    const auto tabBefore = tabLine (s.score);

    // marks go on the notation notes (also when the click was in the tab), not on the tab
    auto dyn = req ("dynamic", idOf (s.score, 1, 2));
    dyn.set ("value", "mf");
    CHECK (s.run (dyn).ok);
    CHECK (s.run (req ("slur", idOf (s.score, 0), "count", 2)).ok);
    auto text = req ("text", idOf (s.score, 2));
    text.set ("text", "riff");
    text.set ("place", "above");
    CHECK (s.run (text).ok);
    CHECK_STR (tabLine (s.score).c_str(), tabBefore.c_str());
    CHECK_STR (tabProblems (s.score).c_str(), "");

    // a key change spells the notation again and leaves the tab alone
    Json key = Json::object();
    key.set ("op", "key");
    key.set ("fifths", -3);
    key.set ("minor", false);
    CHECK (s.run (key).ok);
    CHECK_STR (tabLine (s.score).c_str(), tabBefore.c_str());
    CHECK_STR (tabProblems (s.score).c_str(), "");

    // layout works as for piano
    auto first = req ("break", idOf (s.score, 0));
    first.set ("mode", "system");
    CHECK (! s.run (first).ok);   // the first measure cannot have a break

    // the MEI has the marks once, on the notation staff, and the tab staff with its strings
    const auto mei = scoreToMei (s.score, {});
    CHECK (xmlcheck::checkXml (mei).wellFormed);
    CHECK (mei.find ("<tabGrp") != std::string::npos);
    CHECK (mei.find ("tab.course=") != std::string::npos);

    size_t dynams = 0, slurs = 0, at = 0;

    while ((at = mei.find ("<dynam ", at)) != std::string::npos) { ++dynams; ++at; }
    at = 0;
    while ((at = mei.find ("<slur ", at)) != std::string::npos) { ++slurs; ++at; }

    CHECK_EQ ((int) dynams, 1);
    CHECK_EQ ((int) slurs, 1);
    CHECK (mei.find (">riff</dir>") != std::string::npos);
}
REGISTER (testFrettedMarksKeyAndMei, "edit: guitar marks, key and MEI");

// Random edits on guitar and bass takes, started in the notation and in the tab: both staves stay the same music,
// every state undoes and redoes exactly.
static void testRandomFrettedEdits()
{
    std::mt19937 rng (11);
    int applied = 0, refused = 0, strings = 0;

    for (int take = 0; take < 30; ++take)
    {
        const bool bass = take % 3 == 2;
        std::vector<N> notes;
        const int count = 6 + (int) (rng() % 16);

        for (int i = 0; i < count; ++i)
        {
            const double start = (double) (rng() % 40) * 0.25;
            const double dur = 0.25 * (double) (1 + rng() % 6);
            notes.push_back ({ bass ? 28 + (int) (rng() % 30) : 40 + (int) (rng() % 40), start, dur });
        }

        Session s (fretted (notes, 12.0, bass ? InstrumentType::bass : InstrumentType::guitar));
        CHECK_STR (problems (s.score).c_str(), "");
        CHECK_STR (tabProblems (s.score).c_str(), "");
        const Score start = s.score;
        std::vector<Score> history { start };

        for (int step = 0; step < 50; ++step)
        {
            const auto all = events (s.score, 1 + (int) (rng() % 2));
            const auto* target = all[rng() % all.size()];
            std::string id = target->id;

            if (target->type == nodeType::chord && rng() % 2 == 0)
                id = target->children[rng() % target->children.size()].id;

            Json j = Json::object();
            const auto kind = rng() % 18;

            switch (kind)
            {
                case 0: case 1: j = req ("pitch", id, "semitones", (int) (rng() % 5) - 2); break;
                case 2: j = req ("pitch", id, "semitones", rng() % 2 == 0 ? 12 : -12); break;
                case 3: j = req ("duration", id, "dur", 1 << (rng() % 6)); j.set ("dots", (int) (rng() % 2)); break;
                case 4: j = req ("delete", id); break;
                case 5: j = req ("letter", id); j.set ("letter", std::string (1, "ABCDEFG"[rng() % 7])); break;
                case 6: j = req ("interval", id, "interval", 2 + (int) (rng() % 7)); break;
                case 7: j = req ("tie", id); break;
                case 8: j = req ("respell", id); break;
                case 9: j = req ("beam", id); j.set ("mode", std::string (rng() % 3 == 0 ? "break" : rng() % 2 == 0 ? "join" : "auto")); break;
                case 10: j = req ("voice", id, "voice", 2); break;
                case 11: j = Json::object(); j.set ("op", "key"); j.set ("fifths", (int) (rng() % 15) - 7); j.set ("minor", rng() % 2 == 0); break;
                case 12: j = req ("dynamic", id); j.set ("value", std::string (rng() % 2 == 0 ? "mf" : "pp")); break;
                case 13: j = req ("slur", id, "count", 1 + (int) (rng() % 3)); break;
                case 14: j = req ("break", id); j.set ("mode", std::string (rng() % 2 == 0 ? "system" : "none")); break;
                default: j = stringReq (id, rng() % 2 == 0 ? "up" : "down"); ++strings; break;
            }

            const Score before = s.score;
            const auto result = performEdit (s.score, s.undo, j);

            if (result.ok)
            {
                ++applied;
                history.push_back (s.score);
                CHECK (! (s.score == before));
            }
            else
            {
                ++refused;
                CHECK (s.score == before);
            }

            const auto issue = problems (s.score) + tabProblems (s.score);

            if (! issue.empty())
                std::printf ("    take %d step %d: %s after %s\n", take, step, issue.c_str(), j.dump().c_str());

            CHECK_STR (issue.c_str(), "");
        }

        for (size_t i = history.size() - 1; i > 0; --i)
        {
            CHECK (s.score == history[i]);
            CHECK (s.undo.undo());
        }

        CHECK (s.score == start);

        for (size_t i = 1; i < history.size(); ++i)
        {
            CHECK (s.undo.redo());
            CHECK (s.score == history[i]);
        }
    }

    CHECK (applied > 200);
    CHECK (refused > 20);
    CHECK (strings > 20);
}
REGISTER (testRandomFrettedEdits, "edit: random guitar and bass edits keep the tab right and undo exactly");

static Json drumJson (int note)
{
    const auto map = drumPreset ("gm");
    const auto* entry = map.find (note);
    auto j = Json::object();

    if (entry != nullptr)
    {
        j.set ("note", entry->note);
        j.set ("name", entry->name);
        j.set ("loc", entry->loc);
        j.set ("head", entry->head);
        j.set ("voice", entry->voice);
    }

    return j;
}

static Json drumReq (const char* op, const std::string& id, int note)
{
    auto j = req (op, id);
    j.set ("drum", drumJson (note));
    return j;
}

static Score drumGroove()
{
    std::vector<N> notes;

    for (int i = 0; i < 8; ++i)
        notes.push_back ({ 42, i * 0.5, 0.25 });

    notes.push_back ({ 36, 0, 0.25 });
    notes.push_back ({ 38, 1, 0.25 });
    notes.push_back ({ 36, 2, 0.25 });
    notes.push_back ({ 38, 3, 0.25 });
    notes.push_back ({ 36, 4, 0.25 });
    return transcribeDrums (takeOf (notes, 8.0), {}, drumPreset ("gm")).score;
}

static void testDrumEdits()
{
    Session s (drumGroove());
    const auto start = right (s.score);
    CHECK_STR (start.c_str(), "m1 ( Closed_hi-hat/8 Closed_hi-hat/8 ) ( [Snare Closed_hi-hat]/8 Closed_hi-hat/8 ) ( Closed_hi-hat/8 Closed_hi-hat/8 ) ( [Snare Closed_hi-hat]/8 Closed_hi-hat/8 ) "
                              "Bass_drum_1/4 r/4 Bass_drum_1/4 r/4 | m2 R Bass_drum_1/4 r/4 r/2");
    CHECK (editBlocker (s.score).empty());

    // a drum on the bar of rest of the hands: as long as the beat
    CHECK (s.run (drumReq ("drumAdd", idOf (s.score, 12), 38)).ok);
    CHECK (right (s.score).find ("| m2 Snare/4 r/4 r/2 Bass_drum_1/4") != std::string::npos);

    // the same drum twice is refused; another foot drum joins the kick as a chord
    CHECK (! s.run (drumReq ("drumAdd", idOf (s.score, 12), 36)).ok);
    CHECK (s.run (drumReq ("drumAdd", idOf (s.score, 12), 44)).ok);
    CHECK (right (s.score).find ("[Hi-hat_pedal Bass_drum_1]/4 r/4 r/2") != std::string::npos);

    // a hand drum joins the hi-hat; a drum in a rest of the other voice (the tom is a hand drum: it goes to voice 1 at that time)
    CHECK (s.run (drumReq ("drumAdd", idOf (s.score, 0), 49)).ok);
    CHECK (right (s.score).find ("m1 ( [Closed_hi-hat Crash_cymbal_1]/8 Closed_hi-hat/8 )") != std::string::npos);
    CHECK (s.run (drumReq ("drumAdd", idOf (s.score, 9), 45)).ok);
    CHECK (right (s.score).find ("( [Low_tom Snare Closed_hi-hat]/8 Closed_hi-hat/8 )") != std::string::npos);

    // a hit becomes another drum (the same voice); another voice is refused
    CHECK (s.run (drumReq ("drumSet", idOf (s.score, 12), 37)).ok);
    CHECK (right (s.score).find ("| m2 Side_stick/4") != std::string::npos);
    CHECK (! s.run (drumReq ("drumSet", idOf (s.score, 12), 36)).ok);   // the kick is a foot drum
    CHECK (! s.run (drumReq ("drumSet", idOf (s.score, 12), 37)).ok);   // it is that drum already

    // ghost note: brackets in the score and in the MEI
    CHECK (s.run (req ("ghost", idOf (s.score, 12))).ok);
    CHECK (scoreToMei (s.score, {}).find ("head.mod=\"paren\"") != std::string::npos);
    CHECK (s.run (req ("ghost", idOf (s.score, 12))).ok);
    CHECK (scoreToMei (s.score, {}).find ("head.mod=\"paren\"") == std::string::npos);

    // an accent on a hit is written for drums too
    auto accent = req ("artic", idOf (s.score, 12));
    accent.set ("value", "acc");
    CHECK (s.run (accent).ok);
    CHECK (scoreToMei (s.score, {}).find ("artic=\"acc\"") != std::string::npos);

    // pitched edits are refused; delete and length work
    CHECK (! s.run (req ("pitch", idOf (s.score, 0), "semitones", 1)).ok);
    CHECK (! s.run (req ("interval", idOf (s.score, 0), "interval", 3)).ok);
    CHECK (s.run (req ("delete", idOf (s.score, 1))).ok);
    CHECK (right (s.score).find ("m1 [Closed_hi-hat Crash_cymbal_1]/8 r/8") != std::string::npos);
    CHECK (s.run (req ("duration", idOf (s.score, 12), "dur", 8)).ok);
    CHECK_STR (problems (s.score).c_str(), "");

    // the voice of a drum that has no layer in this measure yet is made
    Session t (transcribeDrums (takeOf ({ { 38, 0, 0.25 }, { 38, 1, 0.25 } }, 4.0), {}, drumPreset ("gm")).score);
    CHECK (dumpScore (t.score).find ("v2") == std::string::npos);
    CHECK (t.run (drumReq ("drumAdd", idOf (t.score, 0), 36)).ok);
    CHECK (dumpScore (t.score).find ("S1 v2: Bass_drum_1/4 r/4 r/2") != std::string::npos);
    CHECK_STR (problems (t.score).c_str(), "");
}
REGISTER (testDrumEdits, "edit: drum hits are added, changed, ghosted and deleted");

static void testRandomDrumEdits()
{
    std::mt19937 rng (21);
    const int drums[] = { 36, 38, 42, 44, 45, 46, 49, 51, 37, 41 };
    int applied = 0, refused = 0;

    for (int take = 0; take < 25; ++take)
    {
        std::vector<N> notes;
        const int count = 8 + (int) (rng() % 24);

        for (int i = 0; i < count; ++i)
            notes.push_back ({ drums[rng() % 10], (double) (rng() % 48) * 0.25, 0.25 });

        Session s (transcribeDrums (takeOf (notes, 12.0), {}, drumPreset ("gm")).score);
        CHECK_STR (problems (s.score).c_str(), "");
        const Score start = s.score;
        std::vector<Score> history { start };

        for (int step = 0; step < 50; ++step)
        {
            const auto all = events (s.score);
            const auto* target = all[rng() % all.size()];
            std::string id = target->id;

            if (target->type == nodeType::chord && rng() % 2 == 0)
                id = target->children[rng() % target->children.size()].id;

            Json j = Json::object();

            switch (rng() % 14)
            {
                case 0: case 1: case 2: j = drumReq ("drumAdd", id, drums[rng() % 10]); break;
                case 3: case 4: j = drumReq ("drumSet", id, drums[rng() % 10]); break;
                case 5: j = req ("ghost", id); break;
                case 6: j = req ("delete", id); break;
                case 7: j = req ("duration", id, "dur", 1 << (rng() % 6)); j.set ("dots", (int) (rng() % 2)); break;
                case 8: j = req ("tie", id); break;
                case 9: j = req ("beam", id); j.set ("mode", std::string (rng() % 3 == 0 ? "break" : rng() % 2 == 0 ? "join" : "auto")); break;
                case 10: j = req ("artic", id); j.set ("value", std::string (rng() % 2 == 0 ? "acc" : "marc")); break;
                case 11: j = req ("dynamic", id); j.set ("value", std::string (rng() % 2 == 0 ? "f" : "pp")); break;
                case 12: j = req ("break", id); j.set ("mode", std::string (rng() % 2 == 0 ? "system" : "none")); break;
                default: j = req ("pitch", id, "semitones", 1); break;   // always refused
            }

            const Score before = s.score;
            const auto result = performEdit (s.score, s.undo, j);

            if (result.ok)
            {
                ++applied;
                history.push_back (s.score);
                CHECK (! (s.score == before));
            }
            else
            {
                ++refused;
                CHECK (s.score == before);
            }

            const auto issue = problems (s.score);

            if (! issue.empty())
                std::printf ("    take %d step %d: %s after %s\n", take, step, issue.c_str(), j.dump().c_str());

            CHECK_STR (issue.c_str(), "");
        }

        for (size_t i = history.size() - 1; i > 0; --i)
        {
            CHECK (s.score == history[i]);
            CHECK (s.undo.undo());
        }

        CHECK (s.score == start);

        for (size_t i = 1; i < history.size(); ++i)
        {
            CHECK (s.undo.redo());
            CHECK (s.score == history[i]);
        }
    }

    CHECK (applied > 200);
    CHECK (refused > 20);
}
REGISTER (testRandomDrumEdits, "edit: random drum edits stay well formed and undo exactly");

//==============================================================================
// Phase 7b: spelling and layout.

#include "core/Mei.h"

static void testRespell()
{
    Session s (pianoScore ({ { p ("C#4"), 0, 1 }, { p ("E4"), 1, 1 }, { p ("D4"), 2, 1 } }, 4.0, 0));
    CHECK_STR (right (s.score).c_str(), "m1 C#4!/4 E4/4 D4/4 r/4");

    auto r = s.run (req ("respell", idOf (s.score, 0)));
    CHECK (r.ok);
    CHECK_STR (right (s.score).c_str(), "m1 Db4!/4 E4/4 D4!/4 r/4");   // the D needs a natural after the D flat
    CHECK (s.run (req ("respell", idOf (s.score, 0))).ok);
    CHECK_STR (right (s.score).c_str(), "m1 C#4!/4 E4/4 D4/4 r/4");

    // a white key has an unusual spelling too; D has none with one accidental
    CHECK (s.run (req ("respell", idOf (s.score, 1))).ok);
    CHECK_STR (right (s.score).c_str(), "m1 C#4!/4 Fb4!/4 D4/4 r/4");
    CHECK (! s.run (req ("respell", idOf (s.score, 2))).ok);
    CHECK (! s.run (req ("respell", idOf (s.score, 3))).ok);   // a rest

    // tied notes are spelled alike
    Session t (pianoScore ({ { p ("C#4"), 0, 1 }, { p ("C#4"), 1, 1 }, { p ("C#4"), 2, 1 } }, 4.0, 0));
    CHECK (t.run (req ("tie", idOf (t.score, 0))).ok);
    CHECK (t.run (req ("tie", idOf (t.score, 1))).ok);
    CHECK (t.run (req ("respell", idOf (t.score, 1))).ok);
    CHECK_STR (right (t.score).c_str(), "m1 C#4!/4~ C#4/4~ C#4/4 r/4");   // the take was spelled with flats; all three change

    // a chord: every note changes
    Session c (pianoScore ({ { p ("C#4"), 0, 1 }, { p ("F#4"), 0, 1 } }, 4.0, 0));
    CHECK (c.run (req ("respell", idOf (c.score, 0))).ok);
    CHECK_STR (right (c.score).c_str(), "m1 [C#4! F#4!]/4 r/4 r/2");
}
REGISTER (testRespell, "edit: other spelling of a pitch, tied notes together");

static void testChangeKey()
{
    Session s (pianoScore ({ { p ("F#4"), 0, 1 }, { p ("G4"), 1, 1 }, { p ("Bb4"), 2, 1 }, { p ("C#5"), 3, 1 } }, 4.0, 0));
    CHECK_STR (right (s.score).c_str(), "m1 Gb4!/4 G4!/4 Bb4!/4 Db5!/4");

    Json k = Json::object();
    k.set ("op", "key");
    k.set ("fifths", 1);
    k.set ("minor", false);
    CHECK (s.run (k).ok);
    CHECK_EQ ((int) s.score.root().prop ("keyFifths").asInt(), 1);
    CHECK_EQ ((int) s.score.root().prop ("keyTonic").asInt(), 7);
    CHECK_STR (s.score.root().prop ("keyMode").asString().c_str(), "major");
    CHECK_STR (right (s.score).c_str(), "m1 F#4/4 G4/4 A#4!/4 C#5!/4");   // F sharp is in G major now

    // flats: the same pitches spelled with flats
    k.set ("fifths", -3);
    k.set ("minor", true);   // C minor
    CHECK (s.run (k).ok);
    CHECK_EQ ((int) s.score.root().prop ("keyTonic").asInt(), 0);
    CHECK_STR (s.score.root().prop ("keyMode").asString().c_str(), "minor");
    CHECK_STR (right (s.score).c_str(), "m1 Gb4!/4 G4!/4 Bb4/4 Db5!/4");

    // the key signature is in the MEI
    CHECK (scoreToMei (s.score, {}).find ("keysig=\"3f\"") != std::string::npos);

    // nothing to do, and out of range
    CHECK (! s.run (k).ok);
    k.set ("fifths", 9);
    CHECK (! s.run (k).ok);

    // the keys of a take of several bars keep the bars as they are
    Session many (pianoScore ({ { p ("E4"), 0, 1 }, { p ("F4"), 4, 1 }, { p ("A#4"), 8, 1 } }, 12.0, 0));
    k.set ("fifths", 2);
    k.set ("minor", false);
    CHECK (many.run (k).ok);
    CHECK (problems (many.score).empty());
}
REGISTER (testChangeKey, "edit: a new key respells the whole score and undoes in one step");

static void testStemAndBeam()
{
    Session s (pianoScore ({ { p ("C4"), 0, 0.5 }, { p ("D4"), 0.5, 0.5 }, { p ("E4"), 1, 0.5 }, { p ("F4"), 1.5, 0.5 } }, 4.0, 0));
    CHECK_STR (right (s.score).c_str(), "m1 ( C4/8 D4/8 ) ( E4/8 F4/8 ) r/2");

    // stems
    auto j = req ("stem", idOf (s.score, 0));
    j.set ("dir", "down");
    CHECK (s.run (j).ok);
    CHECK_STR (events (s.score)[0]->prop ("stem").asString().c_str(), "down");
    CHECK (scoreToMei (s.score, {}).find ("stem.dir=\"down\"") != std::string::npos);
    CHECK (describeNode (s.score, idOf (s.score, 0)).find ("stem down") != std::string::npos);
    j.set ("dir", "auto");
    CHECK (s.run (j).ok);
    CHECK (! events (s.score)[0]->has ("stem"));
    j.set ("dir", "flip");
    CHECK (s.run (j).ok);
    CHECK_STR (events (s.score)[0]->prop ("stem").asString().c_str(), "up");
    CHECK (s.run (j).ok);
    CHECK_STR (events (s.score)[0]->prop ("stem").asString().c_str(), "down");
    CHECK (s.run (j).ok);
    CHECK (! events (s.score)[0]->has ("stem"));
    j.set ("dir", "sideways");
    CHECK (! s.run (j).ok);
    CHECK (! s.run (req ("stem", idOf (s.score, 4))).ok);   // a rest

    // beams: join across the beat, break inside it
    auto b = req ("beam", idOf (s.score, 2));
    b.set ("mode", "join");
    CHECK (s.run (b).ok);
    CHECK_STR (right (s.score).c_str(), "m1 ( C4/8 D4/8 E4/8 F4/8 ) r/2");
    CHECK (s.undo.undo());

    b = req ("beam", idOf (s.score, 1));
    b.set ("mode", "break");
    CHECK (s.run (b).ok);
    CHECK_STR (right (s.score).c_str(), "m1 C4/8 D4/8 ( E4/8 F4/8 ) r/2");
    b.set ("mode", "auto");
    CHECK (s.run (b).ok);
    CHECK_STR (right (s.score).c_str(), "m1 ( C4/8 D4/8 ) ( E4/8 F4/8 ) r/2");
    b.set ("mode", "nonsense");
    CHECK (! s.run (b).ok);

    // the beam choice survives an edit of the same voice
    b = req ("beam", idOf (s.score, 2));
    b.set ("mode", "join");
    CHECK (s.run (b).ok);
    CHECK (s.run (req ("pitch", idOf (s.score, 0), "semitones", 2)).ok);
    CHECK_STR (right (s.score).c_str(), "m1 ( D4/8 D4/8 E4/8 F4/8 ) r/2");
}
REGISTER (testStemAndBeam, "edit: stem direction, beam break and join");

static void testVoices()
{
    Session s (pianoScore ({ { p ("C4"), 0, 1 }, { p ("D4"), 1, 1 }, { p ("E4"), 2, 1 }, { p ("F4"), 3, 1 } }, 4.0, 0));
    const Score original = s.score;

    // E4 into voice 2: voice 1 keeps a rest there
    const auto e4 = idOf (s.score, 2);
    auto j = req ("voice", e4, "voice", 2);
    auto r = s.run (j);
    CHECK (r.ok);
    CHECK_STR (r.select.c_str(), e4.c_str());
    const auto text = dumpScore (s.score);
    CHECK (text.find ("S1 v1: C4/4 D4/4 r/4 F4/4") != std::string::npos);
    CHECK (text.find ("S1 v2: r/2 E4/4 r/4") != std::string::npos);
    CHECK (problems (s.score).empty());

    // another note into voice 2: it joins the first; a note over the first is refused
    const auto c4 = idOf (s.score, 0);
    auto second = req ("voice", c4, "voice", 2);
    CHECK (s.run (second).ok);
    CHECK (dumpScore (s.score).find ("S1 v2: C4/4 r/4 E4/4 r/4") != std::string::npos);

    // back to voice 1: an empty voice 2 disappears and the score is as it was
    CHECK (s.undo.undo());
    CHECK (s.run (req ("voice", e4, "voice", 1)).ok);
    CHECK_STR (dumpScore (s.score).c_str(), dumpScore (original).c_str());
    CHECK (dumpScore (s.score).find ("v2") == std::string::npos);

    // a voice that has a note there already
    CHECK (s.run (req ("voice", idOf (s.score, 1), "voice", 2)).ok);          // D4 into voice 2
    const auto f4 = idOf (s.score, 3);                                          // F4 in voice 1
    CHECK (s.run (req ("voice", f4, "voice", 2)).ok);                           // F4 next to it: free
    CHECK (s.run (req ("duration", idOf (s.score, 5), "dur", 2)).ok);           // lengthen the D4 of voice 2 over the F4: taken out

    // the same voice, a voice that does not exist, a rest
    CHECK (! s.run (req ("voice", idOf (s.score, 0), "voice", 1)).ok);
    CHECK (! s.run (req ("voice", idOf (s.score, 0), "voice", 7)).ok);
}
REGISTER (testVoices, "edit: move a note to another voice and back");

//==============================================================================
// Phase 7c: markings and text.

#include "XmlCheck.h"

static Json reqV (const char* op, const std::string& id, const char* key, const std::string& value)
{
    auto j = req (op, id);
    j.set (key, value);
    return j;
}

static void testMarkings()
{
    Session s (pianoScore ({ { p ("C4"), 0, 1 }, { p ("D4"), 1, 1 }, { p ("E4"), 2, 1 }, { p ("F4"), 3, 1 } }, 4.0, 0));

    // a dynamic: set, change, same again takes it away
    CHECK (s.run (reqV ("dynamic", idOf (s.score, 0), "value", "mf")).ok);
    CHECK_STR (events (s.score)[0]->prop ("dyn").asString().c_str(), "mf");
    CHECK (s.run (reqV ("dynamic", idOf (s.score, 0), "value", "pp")).ok);
    CHECK_STR (events (s.score)[0]->prop ("dyn").asString().c_str(), "pp");
    CHECK (s.run (reqV ("dynamic", idOf (s.score, 0), "value", "pp")).ok);
    CHECK (! events (s.score)[0]->has ("dyn"));
    CHECK (! s.run (reqV ("dynamic", idOf (s.score, 0), "value", "")).ok);          // nothing to take away
    CHECK (! s.run (reqV ("dynamic", idOf (s.score, 0), "value", "loud")).ok);      // not a dynamic
    CHECK (! s.run (reqV ("dynamic", idOf (s.score, 4), "value", "f")).ok);         // a rest

    // articulations: one at a time; the fermata is separate
    CHECK (s.run (reqV ("artic", idOf (s.score, 1), "value", "stacc")).ok);
    CHECK (s.run (reqV ("artic", idOf (s.score, 1), "value", "acc")).ok);
    CHECK_STR (events (s.score)[1]->prop ("artic").asString().c_str(), "acc");
    CHECK (s.run (reqV ("artic", idOf (s.score, 1), "value", "ferm")).ok);
    CHECK (events (s.score)[1]->prop ("fermata").asBool());
    CHECK (s.run (reqV ("artic", idOf (s.score, 1), "value", "acc")).ok);
    CHECK (! events (s.score)[1]->has ("artic"));
    CHECK (! s.run (reqV ("artic", idOf (s.score, 1), "value", "tickle")).ok);

    // text above and below, trimmed, not too long
    auto t = reqV ("text", idOf (s.score, 2), "text", "  dolce  ");
    t.set ("place", "below");
    CHECK (s.run (t).ok);
    CHECK_STR (events (s.score)[2]->prop ("text").asString().c_str(), "dolce");
    CHECK_STR (events (s.score)[2]->prop ("textPlace").asString().c_str(), "below");
    CHECK (describeNode (s.score, idOf (s.score, 2)).find ("text \"dolce\" below") != std::string::npos);
    t.set ("text", std::string (81, 'x'));
    CHECK (! s.run (t).ok);
    t.set ("text", "");
    CHECK (s.run (t).ok);
    CHECK (! events (s.score)[2]->has ("text"));
    CHECK (! s.run (t).ok);

    // clear takes all of it away at once
    CHECK (s.run (reqV ("dynamic", idOf (s.score, 3), "value", "f")).ok);
    CHECK (s.run (reqV ("artic", idOf (s.score, 3), "value", "marc")).ok);
    CHECK (s.run (req ("clear", idOf (s.score, 3))).ok);
    CHECK (! events (s.score)[3]->has ("dyn") && ! events (s.score)[3]->has ("artic"));
    CHECK (! s.run (req ("clear", idOf (s.score, 3))).ok);

    // marks survive a change of length, and go with a deleted note
    CHECK (s.run (reqV ("dynamic", idOf (s.score, 0), "value", "mp")).ok);
    CHECK (s.run (req ("duration", idOf (s.score, 0), "dur", 8)).ok);
    CHECK_STR (events (s.score)[0]->prop ("dyn").asString().c_str(), "mp");
    CHECK (s.run (req ("delete", idOf (s.score, 0))).ok);
    CHECK (! events (s.score)[0]->has ("dyn"));

    // the MEI
    CHECK (s.run (reqV ("dynamic", idOf (s.score, 2), "value", "ff")).ok);
    CHECK (s.run (reqV ("artic", idOf (s.score, 2), "value", "stacc")).ok);
    CHECK (s.run (reqV ("artic", idOf (s.score, 2), "value", "ferm")).ok);
    auto txt = reqV ("text", idOf (s.score, 2), "text", "a & b < c");
    CHECK (s.run (txt).ok);
    const auto mei = scoreToMei (s.score, {});
    CHECK (xmlcheck::checkXml (mei).wellFormed);
    CHECK (mei.find ("<dynam startid=\"#" + idOf (s.score, 2) + "\" staff=\"1\" place=\"below\">ff</dynam>") != std::string::npos);
    CHECK (mei.find ("artic=\"stacc\"") != std::string::npos);
    CHECK (mei.find ("<fermata startid=\"#" + idOf (s.score, 2)) != std::string::npos);
    CHECK (mei.find (">a &amp; b &lt; c</dir>") != std::string::npos);
    CHECK (describeNode (s.score, idOf (s.score, 2)).find ("marks: ff, staccato, fermata") != std::string::npos);
}
REGISTER (testMarkings, "edit: dynamics, articulations, fermata and text");

static void testSlursAndHairpins()
{
    Session s (pianoScore ({ { p ("C4"), 0, 1 }, { p ("D4"), 1, 1 }, { p ("E4"), 2, 1 }, { p ("F4"), 3, 1 }, { p ("G4"), 4, 1 }, { p ("A4"), 5, 1 } }, 8.0, 0));

    auto slur = req ("slur", idOf (s.score, 0), "count", 3);
    CHECK (s.run (slur).ok);
    CHECK (describeNode (s.score, idOf (s.score, 0)).find ("slur starts here") != std::string::npos);
    auto mei = scoreToMei (s.score, {});
    CHECK (xmlcheck::checkXml (mei).wellFormed);
    CHECK (mei.find ("<slur startid=\"#" + idOf (s.score, 0) + "\" endid=\"#" + idOf (s.score, 3) + "\" staff=\"1\"/>") != std::string::npos);

    // the same again takes it away
    CHECK (s.run (slur).ok);
    CHECK (scoreToMei (s.score, {}).find ("<slur") == std::string::npos);

    // across the bar line; the rest of the take is rests, so there are not enough notes after the last one
    CHECK (s.run (req ("slur", idOf (s.score, 2), "count", 3)).ok);
    CHECK (! s.run (req ("slur", idOf (s.score, 5), "count", 1)).ok);
    CHECK (! s.run (req ("slur", idOf (s.score, 0), "count", 0)).ok);

    // hairpins: one per start; the other kind replaces it
    auto cresc = req ("hairpin", idOf (s.score, 1), "count", 2);
    cresc.set ("form", "cresc");
    CHECK (s.run (cresc).ok);
    CHECK (scoreToMei (s.score, {}).find ("<hairpin form=\"cres\"") != std::string::npos);
    auto dim = cresc;
    dim.set ("form", "dim");
    CHECK (s.run (dim).ok);
    const auto both = scoreToMei (s.score, {});
    CHECK (both.find ("form=\"dim\"") != std::string::npos);
    CHECK (both.find ("form=\"cres\"") == std::string::npos);
    CHECK (s.run (dim).ok);
    CHECK (scoreToMei (s.score, {}).find ("<hairpin") == std::string::npos);

    // a slur and a hairpin can start on the same note
    CHECK (s.run (req ("slur", idOf (s.score, 1), "count", 2)).ok);
    CHECK (s.run (cresc).ok);
    mei = scoreToMei (s.score, {});
    CHECK (mei.find ("<slur") != std::string::npos && mei.find ("<hairpin") != std::string::npos);

    // a spanner whose note is gone is not written
    CHECK (s.run (req ("delete", idOf (s.score, 3))).ok);
    CHECK (xmlcheck::checkXml (scoreToMei (s.score, {})).wellFormed);
    CHECK (scoreToMei (s.score, {}).find ("endid=\"#" + idOf (s.score, 3) + "\"") == std::string::npos);
}
REGISTER (testSlursAndHairpins, "edit: slurs and hairpins");

static void testTempo()
{
    Session s (pianoScore ({ { p ("C4"), 0, 1 }, { p ("D4"), 1, 1 }, { p ("E4"), 4, 1 } }, 8.0, 0));
    const auto countTempos = [&]
    {
        int n = 0;

        for (const auto& part : s.score.root().children)
            for (const auto& staff : part.children)
                for (const auto& m : staff.children)
                    for (const auto& c : m.children)
                        n += c.type == nodeType::tempo ? 1 : 0;

        return n;
    };

    CHECK_EQ (countTempos(), 1);   // the transcription's own mark

    auto t = req ("tempo", idOf (s.score, 1), "bpm", 90);
    t.set ("text", "Andante");
    CHECK (s.run (t).ok);
    CHECK_EQ (countTempos(), 2);
    const auto mei = scoreToMei (s.score, {});
    CHECK (xmlcheck::checkXml (mei).wellFormed);
    CHECK (mei.find ("Andante <symbol") != std::string::npos);
    CHECK (mei.find ("mm=\"90\"") != std::string::npos);

    // the same place again replaces the mark; a text only
    auto u = req ("tempo", idOf (s.score, 1), "bpm", 0);
    u.set ("text", "rit.");
    CHECK (s.run (u).ok);
    CHECK_EQ (countTempos(), 2);

    // nothing at all takes it away
    auto none = req ("tempo", idOf (s.score, 1), "bpm", 0);
    none.set ("text", "");
    CHECK (s.run (none).ok);
    CHECK_EQ (countTempos(), 1);
    CHECK (! s.run (none).ok);

    // the first mark of the take can be changed (onset 0 of the first measure)
    auto first = req ("tempo", idOf (s.score, 0), "bpm", 140);
    first.set ("text", "");
    CHECK (s.run (first).ok);
    CHECK_EQ (countTempos(), 1);

    // out of range, a rest
    CHECK (! s.run (req ("tempo", idOf (s.score, 0), "bpm", 500)).ok);
    CHECK (! s.run (req ("tempo", idOf (s.score, 0), "bpm", 5)).ok);
    CHECK (! s.run (req ("tempo", "nonsense", "bpm", 100)).ok);
}
REGISTER (testTempo, "edit: tempo marks");

//==============================================================================
// Phase 7d: page layout.

static std::vector<std::string> breaksOf (const Score& score)
{
    std::vector<std::string> out;

    for (const auto& m : score.root().children[0].children[0].children)
        out.push_back (m.prop ("break").asString());

    return out;
}

static void testBreaks()
{
    Session s (pianoScore ({ { p ("C4"), 0, 1 }, { p ("D4"), 4, 1 }, { p ("E4"), 8, 1 }, { p ("F4"), 12, 1 }, { p ("G4"), 16, 1 } }, 20.0, 0));
    CHECK_EQ (breaksOf (s.score).size(), 5u);

    // a line break before the measure of the selected note; the same again takes it away
    const auto third = events (s.score);
    size_t eIndex = 0;

    for (size_t i = 0; i < third.size(); ++i)
        if (third[i]->type == nodeType::note && third[i]->prop ("pitch").asInt() == 64)
            eIndex = i;

    auto b = req ("break", idOf (s.score, eIndex));
    b.set ("mode", "system");
    CHECK (s.run (b).ok);
    CHECK_STR (breaksOf (s.score)[2].c_str(), "system");
    CHECK (scoreToMei (s.score, {}).find ("<sb/>") != std::string::npos);
    CHECK (xmlcheck::checkXml (scoreToMei (s.score, {})).wellFormed);

    // the break stands right before that measure in the MEI
    const auto mei = scoreToMei (s.score, {});
    const auto at = mei.find ("<sb/>");
    CHECK (mei.substr (mei.find ("<measure", at), 80).find ("n=\"3\"") != std::string::npos);

    CHECK (s.run (b).ok);
    CHECK_STR (breaksOf (s.score)[2].c_str(), "");

    // a page break replaces a line break
    CHECK (s.run (b).ok);
    b.set ("mode", "page");
    CHECK (s.run (b).ok);
    CHECK_STR (breaksOf (s.score)[2].c_str(), "page");
    CHECK (scoreToMei (s.score, {}).find ("<pb/>") != std::string::npos);
    CHECK (scoreToMei (s.score, {}).find ("<sb/>") == std::string::npos);
    b.set ("mode", "none");
    CHECK (s.run (b).ok);
    CHECK (! s.run (b).ok);                       // nothing to take away
    b.set ("mode", "wrongly");
    CHECK (! s.run (b).ok);

    // the first measure cannot have one
    auto first = req ("break", idOf (s.score, 0));
    first.set ("mode", "system");
    CHECK (! s.run (first).ok);
}
REGISTER (testBreaks, "edit: line and page breaks");

static void testBarsPerLine()
{
    Session s (pianoScore ({ { p ("C4"), 0, 1 }, { p ("D4"), 4, 1 }, { p ("E4"), 8, 1 }, { p ("F4"), 12, 1 }, { p ("G4"), 16, 1 }, { p ("A4"), 20, 1 } }, 24.0, 0));
    CHECK_EQ (breaksOf (s.score).size(), 6u);

    Json j = Json::object();
    j.set ("op", "perLine");
    j.set ("count", 2);
    CHECK (s.run (j).ok);
    const auto b = breaksOf (s.score);
    CHECK_STR (b[0].c_str(), "");
    CHECK_STR (b[1].c_str(), "");
    CHECK_STR (b[2].c_str(), "system");
    CHECK_STR (b[3].c_str(), "");
    CHECK_STR (b[4].c_str(), "system");
    CHECK (! s.run (j).ok);                       // already like that

    // a page break stays; other counts replace the line breaks
    // (page breaks are left alone by this operation: see the random test)
    j.set ("count", 3);
    CHECK (s.run (j).ok);
    CHECK_STR (breaksOf (s.score)[2].c_str(), "");
    CHECK_STR (breaksOf (s.score)[3].c_str(), "system");

    // back to the program's choice; out of range
    j.set ("count", 0);
    CHECK (s.run (j).ok);
    for (const auto& x : breaksOf (s.score))
        CHECK_STR (x.c_str(), "");
    j.set ("count", 40);
    CHECK (! s.run (j).ok);
    j.set ("count", -1);
    CHECK (! s.run (j).ok);
}
REGISTER (testBarsPerLine, "edit: measures per line");

static void testSpacing()
{
    Session s (pianoScore ({ { p ("C4"), 0, 1 } }, 4.0, 0));
    auto layoutOf = [&] () -> const Node*
    {
        for (const auto& c : s.score.root().children)
            if (c.type == nodeType::layout)
                return &c;

        return nullptr;
    };

    CHECK (layoutOf() == nullptr);

    Json j = Json::object();
    j.set ("op", "spacing");
    j.set ("system", 22);
    CHECK (s.run (j).ok);
    CHECK (layoutOf() != nullptr);
    CHECK_EQ ((int) layoutOf()->prop ("systemSpacing").asInt(), 22);
    CHECK (! layoutOf()->has ("staffSpacing"));

    j.set ("staff", 14);
    CHECK (s.run (j).ok);
    CHECK_EQ ((int) layoutOf()->prop ("staffSpacing").asInt(), 14);

    CHECK (! s.run (j).ok);                       // the same again
    j.set ("system", 99);
    CHECK (! s.run (j).ok);
    Json nothing = Json::object();
    nothing.set ("op", "spacing");
    CHECK (! s.run (nothing).ok);
    CHECK (s.score.validate().empty());
}
REGISTER (testSpacing, "edit: line and staff spacing");

//==============================================================================
// Random edits on random takes: the score stays well formed, every step undoes and redoes exactly,
// and measures that were not touched stay as they were.
static void testRandomEdits()
{
    std::mt19937 rng (7);
    int applied = 0, refused = 0;

    for (int take = 0; take < 40; ++take)
    {
        std::vector<N> notes;
        const int count = 6 + (int) (rng() % 20);
        const int beats = rng() % 3 == 0 ? 3 : 4;                // some takes in 3/4
        const double shift = rng() % 4 == 0 ? 2.5 : 0.0;          // some start late: a pickup bar

        for (int i = 0; i < count; ++i)
        {
            const double start = shift + (double) (rng() % 60) * 0.25;
            const double dur = 0.25 * (double) (1 + rng() % 6);
            notes.push_back ({ 45 + (int) (rng() % 36), start, dur });
        }

        Session s (pianoScore (notes, 18.0, (int) (rng() % 12), rng() % 2 == 0, beats));
        CHECK_STR (problems (s.score).c_str(), "");
        const Score start = s.score;

        // an edit and its opposite give back exactly the generated score: nothing else is rewritten
        {
            Session probe (start);
            const auto all = events (probe.score);
            const auto* note = all[rng() % all.size()];

            if (note->type == nodeType::note && note->prop ("pitch").asInt() < 90 && note->prop ("tie").asString().empty())   // a tied note loses its tie when it moves
            {
                CHECK (probe.run (req ("pitch", note->id, "semitones", 12)).ok);
                CHECK (probe.run (req ("pitch", note->id, "semitones", -12)).ok);
                CHECK (probe.score == start);
                if (! (probe.score == start))
                    std::printf ("=== before\n%s=== after\n%s", dumpScore (start).c_str(), dumpScore (probe.score).c_str());
            }
        }
        std::vector<Score> history { start };

        for (int step = 0; step < 60; ++step)
        {
            const auto all = events (s.score);
            const auto* target = all[rng() % all.size()];
            std::string id = target->id;

            if (target->type == nodeType::chord && rng() % 2 == 0)
                id = target->children[rng() % target->children.size()].id;

            Json j = Json::object();
            const auto kind = rng() % 22;

            switch (kind)
            {
                case 0: j = req ("pitch", id, "semitones", (int) (rng() % 5) - 2); break;
                case 1: j = req ("pitch", id, "semitones", rng() % 2 == 0 ? 12 : -12); break;
                case 2: j = req ("duration", id, "dur", 1 << (rng() % 6)); j.set ("dots", (int) (rng() % 2)); break;
                case 3: j = req ("delete", id); break;
                case 4: j = req ("letter", id); j.set ("letter", std::string (1, "ABCDEFG"[rng() % 7])); break;
                case 5: j = req ("interval", id, "interval", 2 + (int) (rng() % 7)); break;
                case 6: j = req ("tie", id); break;
                case 7: j = req ("respell", id); break;
                case 8: j = req ("stem", id); j.set ("dir", std::string (rng() % 3 == 0 ? "up" : rng() % 2 == 0 ? "down" : rng() % 2 == 0 ? "auto" : "flip")); break;
                case 9: j = req ("beam", id); j.set ("mode", std::string (rng() % 3 == 0 ? "break" : rng() % 2 == 0 ? "join" : "auto")); break;
                case 10: j = req ("voice", id, "voice", 1 + (int) (rng() % 3)); break;
                case 11: j = Json::object(); j.set ("op", "key"); j.set ("fifths", (int) (rng() % 15) - 7); j.set ("minor", rng() % 2 == 0); break;
                case 12: j = req ("dynamic", id); j.set ("value", std::string (rng() % 2 == 0 ? "mf" : rng() % 2 == 0 ? "pp" : "")); break;
                case 13: j = req ("artic", id); j.set ("value", std::string (rng() % 3 == 0 ? "ferm" : rng() % 2 == 0 ? "stacc" : "acc")); break;
                case 14: j = req ("text", id); j.set ("text", std::string (rng() % 4 == 0 ? "" : "text")); j.set ("place", std::string (rng() % 2 == 0 ? "below" : "above")); break;
                case 15: j = req ("slur", id, "count", 1 + (int) (rng() % 3)); break;
                case 16: j = req ("hairpin", id, "count", 1 + (int) (rng() % 3)); j.set ("form", std::string (rng() % 2 == 0 ? "cresc" : "dim")); break;
                case 17: j = req ("tempo", id, "bpm", rng() % 4 == 0 ? 0 : 60 + (int) (rng() % 100)); j.set ("text", std::string (rng() % 2 == 0 ? "" : "rit.")); break;
                case 18: j = req ("clear", id); break;
                case 19: j = req ("break", id); j.set ("mode", std::string (rng() % 3 == 0 ? "page" : rng() % 2 == 0 ? "system" : "none")); break;
                case 20: j = Json::object(); j.set ("op", "perLine"); j.set ("count", (int) (rng() % 4)); break;
                default: j = Json::object(); j.set ("op", "spacing"); j.set ("system", 6 + (int) (rng() % 30)); j.set ("staff", 6 + (int) (rng() % 20)); break;
            }

            const Score before = s.score;
            const auto result = performEdit (s.score, s.undo, j);

            if (result.ok)
            {
                ++applied;
                history.push_back (s.score);
                CHECK (! (s.score == before));
            }
            else
            {
                ++refused;
                CHECK (s.score == before);
            }

            const auto p = problems (s.score);

            if (! p.empty())
                std::printf ("    take %d step %d: %s after %s\n", take, step, p.c_str(), j.dump().c_str());

            CHECK_STR (p.c_str(), "");
        }

        // undo everything: every state comes back in turn
        for (size_t i = history.size() - 1; i > 0; --i)
        {
            CHECK (s.score == history[i]);
            CHECK (s.undo.undo());
        }

        CHECK (s.score == start);

        // redo everything
        for (size_t i = 1; i < history.size(); ++i)
        {
            CHECK (s.undo.redo());
            CHECK (s.score == history[i]);
        }
    }

    CHECK (applied > 300);
    CHECK (refused > 20);
}
REGISTER (testRandomEdits, "edit: random edits stay well formed and undo exactly");

static void testOtherMeasuresUntouched()
{
    Session s (pianoScore ({ { p ("C4"), 0, 1 }, { p ("D4"), 4, 1 }, { p ("E4"), 8, 1 }, { p ("F4"), 12, 1 } }, 16.0, 0));

    auto blocks = [] (const Score& score)
    {
        std::vector<std::string> out;
        const auto text = dumpScore (score);
        size_t at = 0;

        while (at < text.size())
        {
            auto end = text.find ('\n', at);

            if (end == std::string::npos)
                end = text.size();

            const auto line = text.substr (at, end - at);
            at = end + 1;

            if (line.rfind ("m", 0) == 0)
                out.emplace_back();

            if (! out.empty())
                out.back() += line + "\n";
        }

        return out;
    };

    const auto before = blocks (s.score);
    CHECK (performEdit (s.score, s.undo, req ("pitch", idOf (s.score, 3), "semitones", 1)).ok);
    const auto after = blocks (s.score);
    CHECK_EQ (before.size(), after.size());

    int differing = 0;

    for (size_t i = 0; i < before.size() && i < after.size(); ++i)
        differing += before[i] == after[i] ? 0 : 1;

    CHECK_EQ (differing, 1);
}
REGISTER (testOtherMeasuresUntouched, "edit: only the measure that changed is written");

//==============================================================================
// The practice clip of the Live test sheet: the same steps as written there.
#ifdef TRANSCRIBER_FIXTURES_DIR
#include "MidiReader.h"

namespace
{
    bool startsWith (const std::string& text, const std::string& prefix) { return text.rfind (prefix, 0) == 0; }
}

static void testPracticeClip()
{
    const auto file = midireader::read (std::string (TRANSCRIBER_FIXTURES_DIR) + "/t41-edit-practice.mid");
    CHECK (file.ok);

    Session s (transcribePiano (file.capture, {}).score);
    CHECK_STR (right (s.score).c_str(), "m1 [C4 E4 G4]/4 D4/4 E4/2 | m2 C4/4 C4/4 ( F4/8 G4/8 ) A4/4 | m3 C5/1");

    // 1: the chord: one note on its own, then the whole chord an octave up
    const auto chord = events (s.score)[0];
    CHECK (chord->type == nodeType::chord);
    CHECK (s.run (req ("pitch", chord->children[1].id, "semitones", 1)).ok);                 // E4 -> F4
    CHECK (startsWith (right (s.score), "m1 [C4 F4 G4]/4 D4/4 E4/2"));
    CHECK (s.run (req ("pitch", chord->id, "semitones", 12)).ok);
    CHECK (startsWith (right (s.score), "m1 [C5 F5 G5]/4 D4/4 E4/2"));
    CHECK (s.undo.undo());
    CHECK (s.undo.undo());

    // 2: the D4 up a half step shows a sharp
    CHECK (s.run (req ("pitch", idOf (s.score, 1), "semitones", 1)).ok);
    CHECK (events (s.score)[1]->prop ("accid").asString() == "s");
    CHECK (s.undo.undo());

    // 3: a rest where the D4 was; then a note back with a letter
    CHECK (s.run (req ("delete", idOf (s.score, 1))).ok);
    CHECK (startsWith (right (s.score), "m1 [C4 E4 G4]/4 r/4 E4/2"));
    auto j = req ("letter", idOf (s.score, 1));
    j.set ("letter", "F");
    CHECK (s.run (j).ok);
    CHECK (startsWith (right (s.score), "m1 [C4 E4 G4]/4 F4/4 E4/2"));
    CHECK (s.undo.undo());
    CHECK (s.undo.undo());

    // 4: the half note E4 shortened to a quarter leaves a rest, then lengthened over the rest again
    CHECK (s.run (req ("duration", idOf (s.score, 2), "dur", 4)).ok);
    CHECK (startsWith (right (s.score), "m1 [C4 E4 G4]/4 D4/4 E4/4 r/4"));
    CHECK (s.run (req ("duration", idOf (s.score, 2), "dur", 2)).ok);
    CHECK (startsWith (right (s.score), "m1 [C4 E4 G4]/4 D4/4 E4/2"));
    CHECK (s.undo.undo());
    CHECK (s.undo.undo());

    // 5: the two C4 quarters of bar 2 tied
    CHECK (s.run (req ("tie", idOf (s.score, 3))).ok);
    CHECK_STR (events (s.score)[3]->prop ("tie").asString().c_str(), "i");
    CHECK_STR (events (s.score)[4]->prop ("tie").asString().c_str(), "t");

    // 6: an octave down for the last note, and a third added to it
    CHECK (s.run (req ("pitch", idOf (s.score, 8), "semitones", -12)).ok);
    auto add = req ("interval", idOf (s.score, 8));
    add.set ("interval", 3);
    CHECK (s.run (add).ok);
    CHECK (right (s.score).find ("| m3 [C4 E4]/1") != std::string::npos);
}
REGISTER (testPracticeClip, "edit: the practice clip of the Live test sheet");
#endif

#ifdef TRANSCRIBER_FIXTURES_DIR
// The layout practice clip of the 7b test sheet (the key set to C major first, as the sheet says).
static void testLayoutClip()
{
    const auto file = midireader::read (std::string (TRANSCRIBER_FIXTURES_DIR) + "/t42-edit-layout.mid");
    CHECK (file.ok);
    TranscriptionSettings settings;
    settings.keyTonic = 0;
    Session s (transcribePiano (file.capture, settings).score);
    CHECK (startsWith (right (s.score), "m1 ( C4/8 D4/8 ) ( E4/8 F4/8 ) ( G4/8 A4/8 ) ( B4/8 C5/8 ) | m2 F#4!/4 G4/4 Bb4!/4 B4!/4 | m3 Db5!/2 Db5/2"));

    // beams: E4 joined to the first group
    auto join = req ("beam", idOf (s.score, 2));
    join.set ("mode", "join");
    CHECK (s.run (join).ok);
    CHECK (startsWith (right (s.score), "m1 ( C4/8 D4/8 E4/8 F4/8 ) ( G4/8 A4/8 ) ( B4/8 C5/8 )"));
    CHECK (s.undo.undo());

    // spelling: F#4 of bar 2 other way, then the key of G major
    CHECK (s.run (req ("respell", idOf (s.score, 8))).ok);
    CHECK (right (s.score).find ("| m2 Gb4!/4 G4!/4 Bb4!/4 B4!/4") != std::string::npos);
    CHECK (s.undo.undo());
    Json key = Json::object();
    key.set ("op", "key");
    key.set ("fifths", 1);
    key.set ("minor", false);
    CHECK (s.run (key).ok);
    CHECK (right (s.score).find ("| m2 F#4/4 G4/4 A#4!/4 B4/4") != std::string::npos);

    // voice and stem
    auto voice = req ("voice", idOf (s.score, 6), "voice", 2);
    CHECK (s.run (voice).ok);
    CHECK (dumpScore (s.score).find ("S1 v2:") != std::string::npos);
    auto stem = req ("stem", idOf (s.score, 0));
    stem.set ("dir", "down");
    CHECK (s.run (stem).ok);

    // the two C# of bar 3: tied, then written the other way together
    CHECK (s.run (req ("tie", idOf (s.score, events (s.score).size() - 2))).ok);
    CHECK (s.run (req ("respell", idOf (s.score, events (s.score).size() - 2))).ok);
    CHECK (right (s.score).find ("| m3 Db5") != std::string::npos);
    CHECK (problems (s.score).empty());
}
REGISTER (testLayoutClip, "edit: the layout practice clip of the 7b test sheet");
#endif

#ifdef TRANSCRIBER_FIXTURES_DIR
// The guitar and bass clips of the 7e test sheet: the same steps as written there.
static void testFrettedClips()
{
    const auto file = midireader::read (std::string (TRANSCRIBER_FIXTURES_DIR) + "/t44-edit-guitar.mid");
    CHECK (file.ok);
    Session s (transcribeFretted (file.capture, {}, InstrumentType::guitar).score);
    CHECK_STR (tabLine (s.score).c_str(), "6:0 5:0 4:0 3:0 [5:0 4:2] 2:0 2:1 4:2 3:0 3:2 2:0 3:2 3:0 4:2 4:0");

    // 1: the lowest note cannot go lower; a half step up is fret 1
    CHECK (! s.run (req ("pitch", idOf (s.score, 0), "semitones", -1)).ok);
    CHECK (s.run (req ("pitch", idOf (s.score, 0), "semitones", 1)).ok);
    CHECK (startsWith (tabLine (s.score), "6:1 5:0"));
    CHECK (s.undo.undo());

    // 2: a click in the tab: the A string note up a half step, and the tab note stays selected
    const auto tabClick = idOf (s.score, 1, 2);
    const auto up = s.run (req ("pitch", tabClick, "semitones", 1));
    CHECK (up.ok);
    CHECK (startsWith (tabLine (s.score), "6:0 5:1 4:0"));
    CHECK_STR (up.select.c_str(), idOf (s.score, 1, 2).c_str());
    CHECK (s.undo.undo());

    // 3: the same note on the low E string, and back
    CHECK (s.run (stringReq (idOf (s.score, 1, 2), "down")).ok);
    CHECK (startsWith (tabLine (s.score), "6:0 6:5 4:0"));
    CHECK (s.run (stringReq (idOf (s.score, 1, 2), "up")).ok);
    CHECK (startsWith (tabLine (s.score), "6:0 5:0 4:0"));

    // 4: the chord of bar 2: the chord itself is refused, its low note moves
    const auto chord = events (s.score, 2)[4]->id;
    const auto low = events (s.score, 2)[4]->children[0].id;
    CHECK (! s.run (stringReq (chord, "down")).ok);
    CHECK (s.run (stringReq (low, "down")).ok);
    CHECK (tabLine (s.score).find ("[6:5 4:2]") != std::string::npos);

    // 5: a third above the B3 makes a chord that can be played
    auto third = req ("interval", idOf (s.score, 5));
    third.set ("interval", 3);
    CHECK (s.run (third).ok);
    CHECK (events (s.score, 2)[5]->type == nodeType::chord);
    CHECK_STR (tabProblems (s.score).c_str(), "");

    // 6: a note of bar 3 becomes a rest in both staves
    CHECK (s.run (req ("delete", idOf (s.score, 7))).ok);
    CHECK (right (s.score).find ("r/8") != std::string::npos);
    CHECK (tabLine (s.score).find (" r ") != std::string::npos);

    // 7: a dynamic on a click in the tab: once in the MEI, on the notation staff
    auto dyn = req ("dynamic", idOf (s.score, 2, 2));
    dyn.set ("value", "mf");
    CHECK (s.run (dyn).ok);
    const auto mei = scoreToMei (s.score, {});
    CHECK (mei.find ("<dynam ") != std::string::npos);

    // 8: one voice only; a new key leaves the tab as it is
    CHECK (! s.run (req ("voice", idOf (s.score, 0), "voice", 2)).ok);
    const auto tabNow = tabLine (s.score);
    Json key = Json::object();
    key.set ("op", "key");
    key.set ("fifths", -2);
    key.set ("minor", false);
    CHECK (s.run (key).ok);
    CHECK_STR (tabLine (s.score).c_str(), tabNow.c_str());
    CHECK_STR (tabProblems (s.score).c_str(), "");
    CHECK_STR (problems (s.score).c_str(), "");

    // the bass clip
    const auto bassFile = midireader::read (std::string (TRANSCRIBER_FIXTURES_DIR) + "/t45-edit-bass.mid");
    CHECK (bassFile.ok);
    Session b (transcribeFretted (bassFile.capture, {}, InstrumentType::bass).score);
    CHECK_STR (tabLine (b.score).c_str(), "4:0 3:0 2:0 1:0 4:0 4:3 3:0 4:0 4:0 4:3 3:0 3:2 3:0 4:3 4:0");
    CHECK (! b.run (req ("pitch", idOf (b.score, 0), "semitones", -1)).ok);
    CHECK (b.run (req ("pitch", idOf (b.score, 3), "semitones", 2)).ok);                // G2 up a whole step
    CHECK_STR (tabProblems (b.score).c_str(), "");
    CHECK (b.run (stringReq (idOf (b.score, 1, 2), "down")).ok);                        // A1 on the E string, fret 5
    CHECK (startsWith (tabLine (b.score), "4:0 4:5"));
}
REGISTER (testFrettedClips, "edit: the guitar and bass clips of the Live test sheet");
#endif

#ifdef TRANSCRIBER_FIXTURES_DIR
static void testGrooveClip()
{
    const auto file = midireader::read (std::string (TRANSCRIBER_FIXTURES_DIR) + "/t31-drum-groove.mid");
    CHECK (file.ok);
    Session s (transcribeDrums (file.capture, {}, drumPreset ("gm")).score);
    CHECK (editBlocker (s.score).empty());
    CHECK (right (s.score).find ("m1 ( [Closed_hi-hat Crash_cymbal_1]/8 Closed_hi-hat/8 )") != std::string::npos);

    // 1: a pedal hi-hat (a foot drum) on the rest of beat 2 in the lower voice
    CHECK (s.run (drumReq ("drumAdd", idOf (s.score, 9), 44)).ok);
    CHECK (right (s.score).find ("S1 v2: Bass_drum_1/4 Hi-hat_pedal/4 Bass_drum_1/4 r/4") != std::string::npos || dumpScore (s.score).find ("Bass_drum_1/4 Hi-hat_pedal/4 Bass_drum_1/4 r/4") != std::string::npos);

    // 2: a ride cymbal next to the hi-hat of the first beat's second half
    CHECK (s.run (drumReq ("drumAdd", idOf (s.score, 1), 51)).ok);
    CHECK (dumpScore (s.score).find ("[Ride_cymbal_1 Closed_hi-hat]/8") != std::string::npos);

    // 3: the snare of beat 2 becomes a side stick (click the note of the chord)
    CHECK (s.run (drumReq ("drumSet", events (s.score)[2]->children[0].id, 37)).ok);
    CHECK (dumpScore (s.score).find ("[Side_stick Closed_hi-hat]/8") != std::string::npos);

    // 4: the ghost snare of bar 3 is a normal hit and a ghost again
    const auto ghost = idOf (s.score, 33);
    CHECK (dumpScore (s.score).find ("(Snare)/16") != std::string::npos);
    CHECK (s.run (req ("ghost", ghost)).ok);
    CHECK (dumpScore (s.score).find ("(Snare)/16") == std::string::npos);
    CHECK (s.run (req ("ghost", ghost)).ok);
    CHECK (dumpScore (s.score).find ("(Snare)/16") != std::string::npos);

    // 5: an accent on the snare; a hi-hat deleted; a kick shortened
    auto accent = req ("artic", ghost);
    accent.set ("value", "acc");
    CHECK (s.run (accent).ok);
    CHECK (scoreToMei (s.score, {}).find ("artic=\"acc\"") != std::string::npos);
    CHECK (s.run (req ("delete", idOf (s.score, 3))).ok);
    CHECK (s.run (req ("duration", idOf (s.score, 8), "dur", 8)).ok);

    // 6: the pitch controls are not for drums
    CHECK (! s.run (req ("pitch", idOf (s.score, 3), "semitones", 1)).ok);
    CHECK_STR (problems (s.score).c_str(), "");
    CHECK (xmlcheck::checkXml (scoreToMei (s.score, {})).wellFormed);
}
REGISTER (testGrooveClip, "edit: the drum groove clip of the Live test sheet");
#endif
