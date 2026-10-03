#include "Edit.h"

#include "TranscribeInternal.h"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <memory>
#include <set>

namespace trs
{

int pitchOf (char step, int alter, int octave)
{
    static const int pcs[7] = { 0, 2, 4, 5, 7, 9, 11 };
    const auto at = std::string ("CDEFGAB").find (step);
    return 12 * (octave + 1) + pcs[at == std::string::npos ? 0 : at] + alter;
}

namespace
{
    using detail::keySignatureAlter;

    constexpr int lowestPitch = 21;    // A0 and C8: the range of a piano
    constexpr int highestPitch = 108;

    const char* const eventKeys[] = { "onset", "ticks", "dur", "dots", "tuplet", "tupletNum", "tupletNumbase", "beam" };

    //==========================================================================
    // Note values and spelling

    bool validValue (int dur, int dots)
    {
        return dur >= 1 && dur <= 64 && (dur & (dur - 1)) == 0 && dots >= 0 && dots <= 2;
    }

    int64_t valueTicks (int dur, int dots)
    {
        const int64_t base = 4 * ticksPerQuarter / dur;
        int64_t total = base, add = base;

        for (int i = 0; i < dots; ++i)
        {
            add /= 2;
            total += add;
        }

        return total;
    }

    int letterIndex (char step)
    {
        const auto at = std::string ("CDEFGAB").find (step);
        return at == std::string::npos ? 0 : (int) at;
    }

    struct Spelling
    {
        char step = 'C';
        int alter = 0;
        int octave = 4;
    };

    Spelling spellingFor (int midi, char step, int alter)
    {
        Spelling s;
        s.step = step;
        s.alter = alter;
        s.octave = (midi - alter - (pitchOf (step, 0, 0) - 12)) / 12 - 1;
        return s;
    }

    // A pitch spelled for a key: a note of the scale as the key signature says, anything else with
    // sharps in a sharp key and flats in a flat key.
    Spelling spell (int midi, int fifths)
    {
        const int pc = ((midi % 12) + 12) % 12;

        for (const char letter : std::string ("CDEFGAB"))
        {
            const auto alter = keySignatureAlter (letter, fifths);
            const auto letterPc = pitchOf (letter, 0, 0) - 12;

            if (((letterPc + alter) % 12 + 12) % 12 == pc)
                return spellingFor (midi, letter, alter);
        }

        static const char sharpLetters[12] = { 'C', 'C', 'D', 'D', 'E', 'F', 'F', 'G', 'G', 'A', 'A', 'B' };
        static const int sharpAlters[12]   = { 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0 };
        static const char flatLetters[12]  = { 'C', 'D', 'D', 'E', 'E', 'F', 'G', 'G', 'A', 'A', 'B', 'B' };
        static const int flatAlters[12]    = { 0, -1, 0, -1, 0, 0, -1, 0, -1, 0, -1, 0 };

        return fifths >= 0 ? spellingFor (midi, sharpLetters[pc], sharpAlters[pc])
                           : spellingFor (midi, flatLetters[pc], flatAlters[pc]);
    }

    void setPitch (Node& note, int midi, const Spelling& s)
    {
        note.props["pitch"] = Json (midi);
        note.props["step"] = Json (std::string (1, s.step));
        note.props["alter"] = Json (s.alter);
        note.props["oct"] = Json (s.octave);
    }

    int midiOf (const Node& note)
    {
        return (int) note.prop ("pitch").asInt();
    }

    //==========================================================================
    // The measure of an edit

    struct Geometry
    {
        int num = 4, den = 4;
        int64_t length = 0;          // ticks of the bar as written
        int64_t offset = 0;          // a pickup bar starts part way into its full bar
        int64_t virtualLength = 0;   // the full bar a pickup belongs to
    };

    Geometry geometryOf (const Node& measure)
    {
        Geometry g;
        g.num = (int) measure.prop ("num").asInt (4);
        g.den = (int) measure.prop ("den").asInt (4);
        g.length = measure.prop ("ticks").asInt();
        const auto nominal = g.den > 0 ? (int64_t) g.num * 4 * ticksPerQuarter / g.den : (int64_t) 4 * ticksPerQuarter;
        g.offset = measure.prop ("pickup").asBool() ? nominal - g.length : 0;
        g.virtualLength = measure.prop ("pickup").asBool() ? nominal : g.length;
        return g;
    }

    bool isEvent (const Node& n)
    {
        return n.type == nodeType::note || n.type == nodeType::rest || n.type == nodeType::chord;
    }

    bool inTuplet (const Node& n)
    {
        return ! n.prop ("tuplet").asString().empty();
    }

    // One note, chord or rest of a layer, with where it sits.
    struct Slot
    {
        int64_t on = 0;
        int64_t len = 0;
        Node node;
    };

    std::vector<Slot> slotsOf (const Node& layer, const Geometry& g)
    {
        std::vector<Slot> slots;

        for (const auto& e : layer.children)
        {
            Slot s;
            s.on = e.prop ("onset").asInt();
            s.len = e.prop ("measureRest").asBool() ? g.length : e.prop ("ticks").asInt();
            s.node = e;
            slots.push_back (std::move (s));
        }

        return slots;
    }

    // Beams: runs of eighth notes and shorter inside one beat group.
    void rebeam (Node& layer, const Geometry& g)
    {
        for (auto& e : layer.children)
            e.props.erase ("beam");

        std::vector<int64_t> boundaries;
        int64_t at = 0;

        for (const auto size : beatGroups (g.num, g.den, g.virtualLength))
        {
            if (at - g.offset > 0)
                boundaries.push_back (at - g.offset);

            at += size;
        }

        int counter = 0;
        size_t previousGroup = ~(size_t) 0;
        std::vector<size_t> run;

        auto close = [&]
        {
            if (run.size() >= 2)
            {
                const auto id = "bm" + std::to_string (++counter);

                for (const auto i : run)
                    layer.children[i].props["beam"] = Json (id);
            }

            run.clear();
        };

        for (size_t i = 0; i < layer.children.size(); ++i)
        {
            const auto& e = layer.children[i];

            if (e.type == nodeType::rest || e.prop ("dur").asInt() < 8)
            {
                close();
                previousGroup = ~(size_t) 0;
                continue;
            }

            size_t group = 0;

            for (const auto b : boundaries)
                if (e.prop ("onset").asInt() >= b)
                    ++group;

            if (! run.empty() && group != previousGroup)
                close();

            run.push_back (i);
            previousGroup = group;
        }

        close();
    }

    // Writes the layer again from its slots: gaps become rests, neighbouring rests are joined and
    // written in the usual values, a bar that is all rest becomes a measure rest, beams are redone.
    bool writeLayer (Score& score, Node& layer, const Geometry& g, std::vector<Slot> slots)
    {
        std::stable_sort (slots.begin(), slots.end(), [] (const Slot& a, const Slot& b) { return a.on < b.on; });

        std::vector<Node> out;
        int64_t cursor = 0;
        int64_t restStart = -1, restEnd = 0;
        std::string restId;

        auto addRest = [&] (int64_t from, int64_t to, const std::string& id)
        {
            if (to <= from)
                return;

            if (restStart < 0)
            {
                restStart = from;
                restId = id;
            }

            restEnd = to;
        };

        auto flush = [&]
        {
            if (restStart < 0)
                return;

            bool first = true;

            for (const auto& p : splitLength (restStart + g.offset, restEnd - restStart, g.num, g.den, g.virtualLength, {}, true))
            {
                Node r = score.makeNode (nodeType::rest);

                if (first && ! restId.empty())
                    r.id = restId;

                first = false;
                r.props["onset"] = Json ((int64_t) (p.on - g.offset));
                r.props["ticks"] = Json ((int64_t) p.ticks);
                r.props["dur"] = Json (p.dur);

                if (p.dots > 0)
                    r.props["dots"] = Json (p.dots);

                out.push_back (std::move (r));
            }

            restStart = -1;
            restId.clear();
        };

        for (auto& s : slots)
        {
            if (s.on < cursor || s.len <= 0)
                return false;

            if (s.on > cursor)
                addRest (cursor, s.on, {});

            if (s.node.type == nodeType::rest && ! inTuplet (s.node))
            {
                addRest (s.on, s.on + s.len, s.node.id);
            }
            else
            {
                flush();
                s.node.props["onset"] = Json (s.on);
                s.node.props["ticks"] = Json (s.len);

                for (auto& child : s.node.children)
                    child.props["onset"] = Json (s.on);

                out.push_back (std::move (s.node));
            }

            cursor = s.on + s.len;

            if (cursor > g.length)
                return false;
        }

        if (cursor < g.length)
            addRest (cursor, g.length, {});

        flush();

        if (out.size() == 1 && out.front().type == nodeType::rest && ! inTuplet (out.front()))
        {
            auto& r = out.front();
            r.props.clear();
            r.props["onset"] = Json (0);
            r.props["ticks"] = Json (g.length);
            r.props["measureRest"] = Json (true);
        }

        layer.children = std::move (out);
        rebeam (layer, g);
        return true;
    }

    //==========================================================================
    // Ties

    struct EventRef
    {
        Node* event = nullptr;
        size_t measure = 0;
    };

    using Sequences = std::map<int64_t, std::vector<EventRef>>;   // by voice (layer number), in playing order

    Sequences sequencesOf (std::vector<Node>& measures)
    {
        Sequences all;

        for (size_t mi = 0; mi < measures.size(); ++mi)
            for (auto& layer : measures[mi].children)
                if (layer.type == nodeType::layer)
                    for (auto& e : layer.children)
                        all[layer.prop ("n").asInt (1)].push_back ({ &e, mi });

        return all;
    }

    std::vector<Node*> notesOf (Node& event)
    {
        std::vector<Node*> out;

        if (event.type == nodeType::note)
            out.push_back (&event);
        else if (event.type == nodeType::chord)
            for (auto& n : event.children)
                out.push_back (&n);

        return out;
    }

    bool tieNext (const Node& n) { const auto& t = n.prop ("tie").asString(); return t == "i" || t == "m"; }
    bool tiePrev (const Node& n) { const auto& t = n.prop ("tie").asString(); return t == "t" || t == "m"; }

    void writeTie (Node& n, bool next, bool previous)
    {
        if (next && previous)       n.props["tie"] = Json ("m");
        else if (next)              n.props["tie"] = Json ("i");
        else if (previous)          n.props["tie"] = Json ("t");
        else                        n.props.erase ("tie");
    }

    Node* sameNote (Node& event, int pitch)
    {
        for (auto* n : notesOf (event))
            if (midiOf (*n) == pitch)
                return n;

        return nullptr;
    }

    // Whether the event at position k of a sequence is followed by one in the same or the next measure.
    const EventRef* following (const std::vector<EventRef>& seq, size_t k)
    {
        if (k + 1 >= seq.size())
            return nullptr;

        const auto& a = seq[k];
        const auto& b = seq[k + 1];
        return b.measure == a.measure || b.measure == a.measure + 1 ? &b : nullptr;
    }

    // A tie is kept only where the next event holds the same pitch; the "from" side is worked out from
    // the "to" side. This makes every edit that moves, deletes or lengthens a note safe for ties.
    void normaliseTies (std::vector<Node>& measures)
    {
        auto sequences = sequencesOf (measures);

        for (auto& [voice, seq] : sequences)
        {
            std::set<const Node*> validNext;

            for (size_t k = 0; k < seq.size(); ++k)
            {
                const auto* next = following (seq, k);

                for (auto* n : notesOf (*seq[k].event))
                    if (tieNext (*n) && next != nullptr && sameNote (*next->event, midiOf (*n)) != nullptr)
                        validNext.insert (n);
            }

            for (size_t k = 0; k < seq.size(); ++k)
            {
                const EventRef* previous = nullptr;

                if (k > 0 && following (seq, k - 1) != nullptr)
                    previous = &seq[k - 1];

                for (auto* n : notesOf (*seq[k].event))
                {
                    const auto next = validNext.count (n) != 0;
                    const auto* from = previous != nullptr ? sameNote (*previous->event, midiOf (*n)) : nullptr;
                    writeTie (*n, next, from != nullptr && validNext.count (from) != 0);
                }
            }
        }
    }

    //==========================================================================
    bool sameNodes (const std::vector<Node>& a, const std::vector<Node>& b);

    bool sameNode (const Node& a, const Node& b)
    {
        return a.id == b.id && a.type == b.type && a.props == b.props && sameNodes (a.children, b.children);
    }

    bool sameNodes (const std::vector<Node>& a, const std::vector<Node>& b)
    {
        if (a.size() != b.size())
            return false;

        for (size_t i = 0; i < a.size(); ++i)
            if (! sameNode (a[i], b[i]))
                return false;

        return true;
    }

    EditResult fail (const std::string& message)
    {
        EditResult r;
        r.message = message;
        return r;
    }

    EditResult success (const std::string& message, const std::string& select = {})
    {
        EditResult r;
        r.ok = true;
        r.message = message;
        r.select = select;
        return r;
    }

    // The staff, measure, voice and event an edit works on, with a copy of the measures to change.
    struct Edit
    {
        Score& score;
        const Node* staff = nullptr;
        std::vector<Node> measures;       // copies of the measures of the staff
        size_t mi = 0, li = 0, ei = 0;    // measure, layer, event
        std::string noteId;               // the note of a chord that was selected, if one was
        int fifths = 0;
        Geometry geometry;

        explicit Edit (Score& s) : score (s) {}

        Node& measure() { return measures[mi]; }
        Node& layer()   { return measures[mi].children[li]; }
        Node& event()   { return measures[mi].children[li].children[ei]; }

        // The selected notes: all of a chord, or the one that was picked.
        std::vector<Node*> targets()
        {
            auto& e = event();

            if (e.type == nodeType::chord && ! noteId.empty())
            {
                for (auto& n : e.children)
                    if (n.id == noteId)
                        return { &n };
            }

            return notesOf (e);
        }

        std::vector<Slot> slots() { return slotsOf (layer(), geometry); }
    };

    bool locate (Edit& e, const std::string& id)
    {
        const auto* node = e.score.find (id);

        if (node == nullptr)
            return false;

        if (node->type == nodeType::note)
        {
            const auto* parent = e.score.findParent (id);

            if (parent != nullptr && parent->type == nodeType::chord)
            {
                e.noteId = id;
                node = parent;
            }
        }

        if (! isEvent (*node))
            return false;

        const auto* layer = e.score.findParent (node->id);
        const auto* measure = layer != nullptr ? e.score.findParent (layer->id) : nullptr;
        size_t measureIndex = 0;
        const auto* staff = measure != nullptr ? e.score.findParent (measure->id, &measureIndex) : nullptr;

        if (staff == nullptr || staff->type != nodeType::staff)
            return false;

        e.staff = staff;
        e.measures = staff->children;
        e.mi = measureIndex;

        for (size_t l = 0; l < e.measures[e.mi].children.size(); ++l)
        {
            if (e.measures[e.mi].children[l].id != layer->id)
                continue;

            e.li = l;

            for (size_t i = 0; i < e.layer().children.size(); ++i)
                if (e.layer().children[i].id == node->id)
                    e.ei = i;
        }

        e.fifths = (int) e.score.root().prop ("keyFifths").asInt();
        e.geometry = geometryOf (e.measure());
        return true;
    }

    // The pitch the music was last at before this event in its voice; a default for the clef if none.
    int referencePitch (Edit& e)
    {
        auto sequences = sequencesOf (e.measures);
        auto& seq = sequences[e.layer().prop ("n").asInt (1)];
        const auto* self = &e.event();

        for (size_t k = 0; k < seq.size(); ++k)
        {
            if (seq[k].event != self)
                continue;

            for (size_t j = k; j > 0; --j)
            {
                const auto notes = notesOf (*seq[j - 1].event);

                if (! notes.empty())
                    return midiOf (*notes.back());   // the top note: chords are written low to high
            }
        }

        return e.staff->prop ("clef").asString() == "F" ? 48 : 64;
    }

    // The pitch of a letter nearest to a reference pitch.
    int nearestOfLetter (char step, int alter, int reference, Spelling& spelling)
    {
        int best = pitchOf (step, alter, 4);
        int bestDistance = 1000;

        for (int octave = 0; octave <= 8; ++octave)
        {
            const auto p = pitchOf (step, alter, octave);
            const auto d = std::abs (p - reference);

            if (d < bestDistance || (d == bestDistance && p > best))
            {
                best = p;
                bestDistance = d;
                spelling = { step, alter, octave };
            }
        }

        return best;
    }

    void sortChord (Node& chord)
    {
        std::stable_sort (chord.children.begin(), chord.children.end(), [] (const Node& a, const Node& b) { return midiOf (a) < midiOf (b); });
    }

    bool hasPitchTwice (const Node& chord)
    {
        std::set<int> seen;

        for (const auto& n : chord.children)
            if (! seen.insert (midiOf (n)).second)
                return true;

        return false;
    }

    //==========================================================================
    // The operations. Each changes the copy of the measures and says what happened.

    EditResult changePitch (Edit& e, int semitones)
    {
        auto& event = e.event();

        if (event.type == nodeType::rest)
            return fail ("A rest has no pitch. Press a letter A to G to put a note there.");

        const bool octave = std::abs (semitones) == 12;

        for (auto* n : e.targets())
        {
            const auto midi = midiOf (*n) + semitones;

            if (midi < lowestPitch || midi > highestPitch)
                return fail ("That is outside the range of the piano.");

            const auto s = octave ? Spelling { n->prop ("step").asString()[0], (int) n->prop ("alter").asInt(), (int) n->prop ("oct").asInt() + semitones / 12 }
                                  : spell (midi, e.fifths);
            setPitch (*n, midi, s);
        }

        if (event.type == nodeType::chord)
        {
            if (hasPitchTwice (event))
                return fail ("That pitch is already in the chord.");

            sortChord (event);
        }

        return success ("Pitch changed.");
    }

    EditResult setLetter (Edit& e, char letter, const Json& alterRequest)
    {
        if (std::string ("ABCDEFG").find (letter) == std::string::npos)
            return fail ("A letter from A to G is needed.");

        const auto alter = alterRequest.isNumber() ? (int) std::max<int64_t> (-2, std::min<int64_t> (2, alterRequest.asInt()))
                                                   : keySignatureAlter (letter, e.fifths);
        auto& event = e.event();

        if (event.type == nodeType::rest)
        {
            auto slots = e.slots();
            auto& slot = slots[e.ei];
            Spelling s;
            const auto midi = nearestOfLetter (letter, alter, referencePitch (e), s);

            if (midi < lowestPitch || midi > highestPitch)
                return fail ("That is outside the range of the piano.");

            Node note;
            note.id = event.id;
            note.type = nodeType::note;
            const int64_t fullLength = slot.len;
            int64_t noteLength = slot.len;

            if (event.prop ("measureRest").asBool())
            {
                // a whole bar of rest: the note takes the first value that fits, the rest stays rest
                const auto pieces = splitLength (slot.on + e.geometry.offset, slot.len, e.geometry.num, e.geometry.den, e.geometry.virtualLength, {}, false);
                note.props["dur"] = Json (pieces.front().dur);

                if (pieces.front().dots > 0)
                    note.props["dots"] = Json (pieces.front().dots);

                noteLength = pieces.front().ticks;
            }
            else
            {
                for (const auto* key : eventKeys)
                    if (event.has (key))
                        note.props[key] = event.prop (key);
            }

            setPitch (note, midi, s);
            note.props["vel"] = Json (90);
            slot.node = note;
            slot.len = noteLength;

            if (noteLength < fullLength)
            {
                Slot rest;
                rest.on = slot.on + noteLength;
                rest.len = fullLength - noteLength;
                rest.node.type = nodeType::rest;
                slots.push_back (std::move (rest));
            }

            if (! writeLayer (e.score, e.layer(), e.geometry, std::move (slots)))
                return fail ("The note does not fit there.");

            return success ("Note entered.", note.id);
        }

        auto targets = e.targets();

        if (targets.size() != 1)
            return fail ("A chord has several notes. Click one of its notes a second time to select just that one.");

        Spelling s;
        const auto midi = nearestOfLetter (letter, alter, midiOf (*targets.front()), s);

        if (midi < lowestPitch || midi > highestPitch)
            return fail ("That is outside the range of the piano.");

        setPitch (*targets.front(), midi, s);

        if (event.type == nodeType::chord)
        {
            if (hasPitchTwice (event))
                return fail ("That pitch is already in the chord.");

            sortChord (event);
        }

        return success ("Pitch changed.");
    }

    EditResult setDuration (Edit& e, int dur, int dots)
    {
        if (! validValue (dur, dots))
            return fail ("That note value does not exist.");

        auto slots = e.slots();
        auto& target = slots[e.ei];

        if (inTuplet (target.node))
            return fail ("The length of a note inside a triplet cannot be changed yet.");

        const auto newLength = valueTicks (dur, dots);

        if (newLength == target.len && ! target.node.prop ("measureRest").asBool()
            && target.node.prop ("dur").asInt() == dur && target.node.prop ("dots").asInt() == dots)
            return fail ("It already has that length.");

        const auto end = target.on + newLength;

        if (end > e.geometry.length)
            return fail ("That does not fit in the measure.");

        int removed = 0;
        const auto oldLength = target.len;
        const auto id = target.node.id;

        target.node.props.erase ("measureRest");
        target.node.props["dur"] = Json (dur);

        if (dots > 0)
            target.node.props["dots"] = Json (dots);
        else
            target.node.props.erase ("dots");

        target.len = newLength;

        if (newLength < oldLength)
        {
            Slot rest;
            rest.on = end;
            rest.len = oldLength - newLength;
            rest.node.type = nodeType::rest;
            slots.push_back (std::move (rest));
        }
        else
        {
            std::vector<Slot> kept;

            for (size_t i = 0; i < slots.size(); ++i)
            {
                auto& s = slots[i];

                if (i <= e.ei || s.on >= end)
                {
                    kept.push_back (std::move (s));
                    continue;
                }

                if (inTuplet (s.node))
                    return fail ("It would run into a triplet; the length of notes inside a triplet cannot be changed yet.");

                if (s.node.type != nodeType::rest)
                    ++removed;

                if (s.on + s.len > end)
                {
                    Slot rest;
                    rest.on = end;
                    rest.len = s.on + s.len - end;
                    rest.node.type = nodeType::rest;
                    kept.push_back (std::move (rest));
                }
            }

            slots = std::move (kept);
        }

        if (! writeLayer (e.score, e.layer(), e.geometry, std::move (slots)))
            return fail ("That does not fit in the measure.");

        return success (removed == 0 ? "Length changed."
                                     : removed == 1 ? "Length changed; the note after it was taken out to make room."
                                                    : "Length changed; " + std::to_string (removed) + " notes after it were taken out to make room.", id);
    }

    EditResult toggleDot (Edit& e)
    {
        const auto& event = e.event();
        return setDuration (e, (int) event.prop ("dur").asInt (4), event.prop ("dots").asInt() > 0 ? 0 : 1);
    }

    EditResult deleteNote (Edit& e)
    {
        auto& event = e.event();

        if (event.type == nodeType::rest)
            return fail ("It is already a rest.");

        if (event.type == nodeType::chord && ! e.noteId.empty() && event.children.size() > 1)
        {
            auto& kids = event.children;
            kids.erase (std::remove_if (kids.begin(), kids.end(), [&] (const Node& n) { return n.id == e.noteId; }), kids.end());

            if (kids.size() == 1)
            {
                // one note is left: it is a plain note again
                Node note = kids.front();

                for (const auto* key : eventKeys)
                    if (event.has (key))
                        note.props[key] = event.prop (key);

                event = std::move (note);
            }

            return success ("Note taken out.", event.id);
        }

        Node rest;
        rest.id = event.id;
        rest.type = nodeType::rest;

        for (const auto* key : eventKeys)
            if (event.has (key) && std::string (key) != "beam")
                rest.props[key] = event.prop (key);

        const auto on = event.prop ("onset").asInt();
        event = std::move (rest);

        if (! writeLayer (e.score, e.layer(), e.geometry, e.slots()))
            return fail ("The measure could not be written again.");

        for (const auto& n : e.layer().children)
            if (n.prop ("onset").asInt() <= on && on < n.prop ("onset").asInt() + n.prop ("ticks").asInt())
                return success ("Made a rest.", n.id);

        return success ("Made a rest.");
    }

    EditResult addInterval (Edit& e, int interval)
    {
        auto& event = e.event();

        if (event.type == nodeType::rest)
            return fail ("Select a note first.");

        if (interval < 2 || interval > 8)
            return fail ("An interval from a 2nd to an octave is needed.");

        auto notes = notesOf (event);
        auto* top = notes.back();
        const auto steps = letterIndex (top->prop ("step").asString()[0]) + interval - 1;
        const char letter = "CDEFGAB"[steps % 7];
        const auto octave = (int) top->prop ("oct").asInt() + steps / 7;
        const auto alter = keySignatureAlter (letter, e.fifths);
        const auto midi = pitchOf (letter, alter, octave);

        if (midi <= midiOf (*top))
            return fail ("That note would not be above the top note.");

        if (midi > highestPitch)
            return fail ("That is outside the range of the piano.");

        Node added = e.score.makeNode (nodeType::note);
        setPitch (added, midi, { letter, alter, octave });
        added.props["vel"] = top->prop ("vel").isNumber() ? top->prop ("vel") : Json (90);
        added.props["onset"] = event.prop ("onset");

        if (event.type == nodeType::note)
        {
            Node chord = e.score.makeNode (nodeType::chord);

            for (const auto* key : eventKeys)
                if (event.has (key))
                    chord.props[key] = event.prop (key);

            Node old = event;

            for (const auto* key : eventKeys)
                old.props.erase (key);

            old.props["onset"] = chord.prop ("onset");
            chord.children.push_back (std::move (old));
            chord.children.push_back (std::move (added));
            event = std::move (chord);
        }
        else
        {
            event.children.push_back (std::move (added));
        }

        if (hasPitchTwice (event))
            return fail ("That pitch is already in the chord.");

        sortChord (event);
        return success ("Note added.", event.id);
    }

    EditResult toggleTie (Edit& e)
    {
        auto& event = e.event();

        if (event.type == nodeType::rest)
            return fail ("A rest cannot be tied.");

        auto sequences = sequencesOf (e.measures);
        auto& seq = sequences[e.layer().prop ("n").asInt (1)];
        size_t at = 0;

        while (at < seq.size() && seq[at].event != &event)
            ++at;

        const auto* next = at < seq.size() ? following (seq, at) : nullptr;
        int done = 0;

        for (auto* n : e.targets())
        {
            if (tieNext (*n))
            {
                writeTie (*n, false, tiePrev (*n));
                ++done;
            }
            else if (next != nullptr && sameNote (*next->event, midiOf (*n)) != nullptr)
            {
                writeTie (*n, true, tiePrev (*n));
                ++done;
            }
        }

        if (done == 0)
            return fail ("There is no note of the same pitch right after it to tie to.");

        return success ("Tie changed.");
    }

    //==========================================================================
    EditResult finish (Edit& e, UndoManager& undo, const std::string& name, EditResult result)
    {
        if (! result.ok)
            return result;

        normaliseTies (e.measures);

        for (auto& m : e.measures)
            detail::refreshAccidentals (m, e.fifths);

        std::vector<ReplaceChildrenCommand::Change> changes;

        for (size_t i = 0; i < e.measures.size(); ++i)
            if (! sameNodes (e.measures[i].children, e.staff->children[i].children))
                changes.push_back ({ e.measures[i].id, e.measures[i].children });

        if (changes.empty())
            return fail ("Nothing changed.");

        if (! undo.perform (std::make_unique<ReplaceChildrenCommand> (std::move (changes), name)))
            return fail ("The change did not fit the score.");

        return result;
    }
}

//==============================================================================
std::string editBlocker (const Score& score)
{
    const Node* part = nullptr;

    for (const auto& c : score.root().children)
        if (c.type == nodeType::part)
            part = &c;

    if (part == nullptr || part->children.empty())
        return "There is no score to edit.";

    for (const auto& staff : part->children)
        if (staff.prop ("kind").asString() == "tab" || staff.prop ("kind").asString() == "perc"
            || staff.prop ("clef").asString() == "TAB" || staff.prop ("clef").asString() == "perc")
            return "Editing is for piano scores for now. Guitar, bass and drum scores come later.";

    return {};
}

EditResult performEdit (Score& score, UndoManager& undo, const Json& request)
{
    const auto& op = request.get ("op").asString();

    if (op == "undo" || op == "redo")
    {
        const auto name = op == "undo" ? undo.undoName() : undo.redoName();

        if (! (op == "undo" ? undo.undo() : undo.redo()))
            return fail (op == "undo" ? "There is nothing to undo." : "There is nothing to redo.");

        return success (std::string (op == "undo" ? "Undid: " : "Redid: ") + name + ".");
    }

    if (const auto blocked = editBlocker (score); ! blocked.empty())
        return fail (blocked);

    Edit e (score);

    if (! locate (e, request.get ("id").asString()))
        return fail ("Select a note or a rest first.");

    if (op == "pitch")
        return finish (e, undo, "Change pitch", changePitch (e, (int) request.get ("semitones").asInt()));

    if (op == "letter")
    {
        const auto& letter = request.get ("letter").asString();
        return finish (e, undo, "Enter note", setLetter (e, letter.empty() ? ' ' : letter[0], request.get ("alter")));
    }

    if (op == "duration")
        return finish (e, undo, "Change length", setDuration (e, (int) request.get ("dur").asInt(), (int) request.get ("dots").asInt()));

    if (op == "dot")
        return finish (e, undo, "Dot", toggleDot (e));

    if (op == "delete")
        return finish (e, undo, "Delete", deleteNote (e));

    if (op == "interval")
        return finish (e, undo, "Add note to chord", addInterval (e, (int) request.get ("interval").asInt()));

    if (op == "tie")
        return finish (e, undo, "Tie", toggleTie (e));

    return fail ("Unknown edit \"" + op + "\".");
}

}  // namespace trs
