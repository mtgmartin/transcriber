#include "Edit.h"

#include "Instruments.h"
#include "Notation.h"
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

    const char* const eventKeys[] = { "onset", "ticks", "dur", "dots", "tuplet", "tupletNum", "tupletNumbase", "beam",
                                      "stem", "beamBreak", "beamJoin", "dyn", "artic", "fermata", "text", "textPlace" };

    // The properties of a note or chord that mean nothing on a rest.
    bool onlyForNotes (const std::string& key)
    {
        return key == "beam" || key == "stem" || key == "beamBreak" || key == "beamJoin" || key == "dyn" || key == "artic"
               || key == "fermata" || key == "text" || key == "textPlace";
    }

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

            // a beam is cut at the edge of a beat group, or where the user asked for it (and kept together across
            // that edge where the user asked for that)
            const bool cut = e.prop ("beamBreak").asBool() || (group != previousGroup && ! e.prop ("beamJoin").asBool());

            if (! run.empty() && cut)
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

    //==========================================================================
    // Guitar and bass: the notation staff is edited, the tab staff under it follows.

    bool isTabStaff (const Node& staff)
    {
        return staff.prop ("kind").asString() == "tab" || staff.prop ("clef").asString() == "TAB";
    }

    const Node* tabStaffOf (const Node& part)
    {
        for (const auto& s : part.children)
            if (isTabStaff (s))
                return &s;

        return nullptr;
    }

    const Node* notationStaffOf (const Node& part)
    {
        for (const auto& s : part.children)
            if (! isTabStaff (s))
                return &s;

        return nullptr;
    }

    // The tab note, chord or rest that belongs to a notation one.
    std::string tabIdOf (const std::string& id)
    {
        return id + "-t";
    }

    // The id of the notation note, chord or rest that a note, chord or rest of the tab stands for; the id itself if
    // it is not in a tab staff (or has no partner).
    std::string notationIdOf (const Score& score, const std::string& id)
    {
        const auto* node = score.find (id);

        if (node == nullptr)
            return id;

        std::vector<size_t> path;   // the index in the parent, going up to the measure in its staff
        const Node* staff = nullptr;
        std::string current = id;

        for (;;)
        {
            size_t index = 0;
            const auto* parent = score.findParent (current, &index);

            if (parent == nullptr)
                return id;

            path.push_back (index);

            if (parent->type == nodeType::staff)
            {
                staff = parent;
                break;
            }

            current = parent->id;
        }

        if (! isTabStaff (*staff))
            return id;

        const auto* part = score.findParent (staff->id);
        const auto* notation = part != nullptr ? notationStaffOf (*part) : nullptr;

        if (notation == nullptr)
            return id;

        const Node* at = notation;

        for (size_t k = path.size(); k-- > 0;)
        {
            if (path[k] >= at->children.size())
                return id;

            at = &at->children[path[k]];
        }

        return at->type == node->type ? at->id : id;
    }

    std::vector<int> openStringsOf (const Node& tabStaff)
    {
        return openStrings (tabStaff.prop ("strings").asInt (6) == 4 ? InstrumentType::bass : InstrumentType::guitar);
    }

    // The staff, measure, voice and event an edit works on, with a copy of the measures to change.
    struct Edit
    {
        Score& score;
        const Node* part = nullptr;
        const Node* tab = nullptr;        // the tab staff of a guitar or bass score
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

        if (staff == nullptr || staff->type != nodeType::staff || isTabStaff (*staff))
            return false;

        e.part = e.score.findParent (staff->id);
        e.tab = e.part != nullptr ? tabStaffOf (*e.part) : nullptr;
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

        const auto clef = e.staff->prop ("clef").asString();
        return clef == "F" ? 48 : clef == "F8" ? 38 : clef == "G8" ? 55 : 64;
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
            if (event.has (key) && ! onlyForNotes (key))
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
    // Spelling and layout (7b)

    // The spellings of a pitch with at most one sharp or flat, the sharpest first.
    std::vector<Spelling> spellingsOf (int midi)
    {
        std::vector<Spelling> out;

        for (int alter = 1; alter >= -1; --alter)
            for (const char letter : std::string ("CDEFGAB"))
            {
                const auto letterPc = pitchOf (letter, 0, 0) - 12;

                if (((letterPc + alter) % 12 + 12) % 12 == ((midi % 12) + 12) % 12)
                    out.push_back (spellingFor (midi, letter, alter));
            }

        return out;
    }

    // A tie joins notes of one pitch, so they are always spelled alike: the spelling follows the tie both ways.
    void spellAlongTies (Edit& e, int pitch, const Spelling& s)
    {
        auto sequences = sequencesOf (e.measures);
        auto& seq = sequences[e.layer().prop ("n").asInt (1)];
        size_t at = 0;

        while (at < seq.size() && seq[at].event != &e.event())
            ++at;

        if (at >= seq.size())
            return;

        for (size_t j = at; j < seq.size(); ++j)
        {
            auto* n = sameNote (*seq[j].event, pitch);

            if (n == nullptr)
                break;

            setPitch (*n, pitch, s);

            if (! tieNext (*n) || following (seq, j) == nullptr)
                break;
        }

        for (size_t j = at; j > 0; --j)
        {
            if (following (seq, j - 1) == nullptr)
                break;

            auto* p = sameNote (*seq[j - 1].event, pitch);

            if (p == nullptr || ! tieNext (*p))
                break;

            setPitch (*p, pitch, s);
        }
    }

    EditResult respell (Edit& e)
    {
        if (e.event().type == nodeType::rest)
            return fail ("A rest has no spelling.");

        int changed = 0;

        for (auto* n : e.targets())
        {
            const auto midi = midiOf (*n);
            const auto options = spellingsOf (midi);

            if (options.size() < 2)
                continue;

            size_t at = 0;

            for (size_t i = 0; i < options.size(); ++i)
                if (options[i].step == n->prop ("step").asString()[0] && options[i].alter == (int) n->prop ("alter").asInt())
                    at = i;

            const auto next = options[(at + 1) % options.size()];
            setPitch (*n, midi, next);
            spellAlongTies (e, midi, next);   // the tied notes before and after change with it
            ++changed;
        }

        if (changed == 0)
            return fail ("That pitch has only one usual spelling.");

        return success ("Spelling changed.");
    }

    EditResult setStem (Edit& e, const std::string& direction)
    {
        auto& event = e.event();

        if (event.type == nodeType::rest)
            return fail ("A rest has no stem.");

        std::string next = direction;

        if (direction == "flip")
        {
            const auto current = event.prop ("stem").asString();
            next = current.empty() ? "up" : current == "up" ? "down" : "auto";
        }

        if (next != "up" && next != "down" && next != "auto")
            return fail ("The stem can point up, down or be left to the program.");

        if (next == "auto")
            event.props.erase ("stem");
        else
            event.props["stem"] = Json (next);

        return success (next == "auto" ? "The stem is left to the program." : "The stem points " + next + ".");
    }

    EditResult setBeam (Edit& e, const std::string& mode)
    {
        auto& event = e.event();

        if (event.type == nodeType::rest)
            return fail ("A rest has no beam.");

        if (mode != "break" && mode != "join" && mode != "auto")
            return fail ("A beam can be broken before the note, joined to the note before, or left to the program.");

        event.props.erase ("beamBreak");
        event.props.erase ("beamJoin");

        if (mode == "break")
            event.props["beamBreak"] = Json (true);
        else if (mode == "join")
            event.props["beamJoin"] = Json (true);

        rebeam (e.layer(), e.geometry);
        return success (mode == "break" ? "The beam is broken before this note."
                                        : mode == "join" ? "This note is joined to the one before it." : "The beam is left to the program.");
    }

    EditResult moveToVoice (Edit& e, int voice)
    {
        if (voice < 1 || voice > 4)
            return fail ("A voice from 1 to 4 is needed.");

        const auto& event = e.event();

        if (event.type == nodeType::rest)
            return fail ("Only notes and chords can move to another voice.");

        if (e.layer().prop ("n").asInt (1) == voice)
            return fail ("It is already in voice " + std::to_string (voice) + ".");

        if (inTuplet (event))
            return fail ("A note inside a triplet cannot change voice yet.");

        auto slotsA = e.slots();
        Slot moving = slotsA[e.ei];
        const auto from = moving.on, to = moving.on + moving.len;

        // the other voice of this measure, or a new one
        auto& children = e.measure().children;
        size_t target = children.size();

        for (size_t i = 0; i < children.size(); ++i)
            if (children[i].type == nodeType::layer && children[i].prop ("n").asInt (1) == voice)
                target = i;

        Node fresh;
        const bool isNew = target == children.size();

        if (isNew)
        {
            fresh = e.score.makeNode (nodeType::layer);
            fresh.props["n"] = Json (voice);
        }

        auto slotsB = isNew ? std::vector<Slot>() : slotsOf (children[target], e.geometry);
        std::vector<Slot> keptB;

        for (auto& s : slotsB)
        {
            if (s.on + s.len <= from || s.on >= to)
            {
                keptB.push_back (std::move (s));
                continue;
            }

            if (s.node.type != nodeType::rest || inTuplet (s.node))
                return fail ("Voice " + std::to_string (voice) + " has a note there already.");

            // only the part of the rest outside the moved note stays a rest
            if (s.on < from)
            {
                Slot before;
                before.on = s.on;
                before.len = from - s.on;
                before.node.type = nodeType::rest;
                before.node.id = s.node.id;
                keptB.push_back (std::move (before));
            }

            if (s.on + s.len > to)
            {
                Slot after;
                after.on = to;
                after.len = s.on + s.len - to;
                after.node.type = nodeType::rest;
                keptB.push_back (std::move (after));
            }
        }

        keptB.push_back (std::move (moving));

        // where the note was, a rest stays
        slotsA[e.ei].node = Node();
        slotsA[e.ei].node.type = nodeType::rest;

        const auto layerA = e.li;
        const auto movedId = e.event().id;

        if (! writeLayer (e.score, e.measure().children[layerA], e.geometry, std::move (slotsA)))
            return fail ("The measure could not be written again.");

        if (isNew)
        {
            if (! writeLayer (e.score, fresh, e.geometry, std::move (keptB)))
                return fail ("The measure could not be written again.");

            size_t at = 0;

            for (size_t i = 0; i < children.size(); ++i)
                if (children[i].type == nodeType::layer && children[i].prop ("n").asInt (1) < voice)
                    at = i + 1;

            children.insert (children.begin() + (std::ptrdiff_t) at, std::move (fresh));
        }
        else if (! writeLayer (e.score, children[target], e.geometry, std::move (keptB)))
        {
            return fail ("The measure could not be written again.");
        }

        // a voice above the first that is only rest is not written
        children.erase (std::remove_if (children.begin(), children.end(), [] (const Node& n)
        {
            return n.type == nodeType::layer && n.prop ("n").asInt (1) > 1 && n.children.size() == 1
                   && n.children.front().type == nodeType::rest && n.children.front().prop ("measureRest").asBool();
        }), children.end());

        return success ("Moved to voice " + std::to_string (voice) + ".", movedId);
    }

    //==========================================================================
    // Markings and text (7c)

    EditResult setDynamic (Edit& e, const std::string& value)
    {
        static const std::set<std::string> known = { "ppp", "pp", "p", "mp", "mf", "f", "ff", "fff", "sfz", "fp" };
        auto& event = e.event();

        if (event.type == nodeType::rest)
            return fail ("A dynamic belongs to a note or chord.");

        if (value.empty() || event.prop ("dyn").asString() == value)
        {
            if (! event.has ("dyn"))
                return fail ("There is no dynamic to take away.");

            event.props.erase ("dyn");
            return success ("Dynamic taken away.");
        }

        if (known.count (value) == 0)
            return fail ("That dynamic is not known.");

        event.props["dyn"] = Json (value);
        return success ("Dynamic " + value + ".");
    }

    // An articulation (one per note; the same one again takes it away), or the fermata (separate).
    EditResult setArticulation (Edit& e, const std::string& value)
    {
        auto& event = e.event();

        if (event.type == nodeType::rest)
            return fail ("An articulation belongs to a note or chord.");

        if (value == "ferm")
        {
            if (event.prop ("fermata").asBool())
            {
                event.props.erase ("fermata");
                return success ("Fermata taken away.");
            }

            event.props["fermata"] = Json (true);
            return success ("Fermata.");
        }

        if (value != "stacc" && value != "acc" && value != "ten" && value != "marc")
            return fail ("That articulation is not known.");

        if (event.prop ("artic").asString() == value)
        {
            event.props.erase ("artic");
            return success ("Articulation taken away.");
        }

        event.props["artic"] = Json (value);
        return success ("Articulation set.");
    }

    EditResult setText (Edit& e, std::string text, const std::string& place)
    {
        auto& event = e.event();

        while (! text.empty() && (text.front() == ' ' || text.front() == '\t'))
            text.erase (text.begin());

        while (! text.empty() && (text.back() == ' ' || text.back() == '\t'))
            text.pop_back();

        if (text.empty())
        {
            if (! event.has ("text"))
                return fail ("There is no text to take away.");

            event.props.erase ("text");
            event.props.erase ("textPlace");
            return success ("Text taken away.");
        }

        if (text.size() > 80)
            return fail ("The text can be 80 characters long at most.");

        event.props["text"] = Json (text);

        if (place == "below")
            event.props["textPlace"] = Json ("below");
        else
            event.props.erase ("textPlace");

        return success ("Text set.");
    }

    EditResult clearMarks (Edit& e)
    {
        auto& event = e.event();
        bool any = false;

        for (const auto* key : { "dyn", "artic", "fermata", "text", "textPlace" })
        {
            any = any || event.has (key);
            event.props.erase (key);
        }

        if (! any)
            return fail ("There is nothing to take away.");

        return success ("Markings taken away.");
    }

    // A slur or hairpin from the selected note or chord to the count-th note or chord after it in the same voice.
    // Doing the same again takes it away. One undo step.
    EditResult spanner (Edit& e, UndoManager& undo, const std::string& kind, int count)
    {
        if (e.event().type == nodeType::rest)
            return fail ("A slur or hairpin starts on a note or chord.");

        if (count < 1 || count > 32)
            return fail ("A slur or hairpin can run over 1 to 32 notes.");

        auto sequences = sequencesOf (e.measures);
        auto& seq = sequences[e.layer().prop ("n").asInt (1)];
        size_t at = 0;

        while (at < seq.size() && seq[at].event != &e.event())
            ++at;

        std::string target;
        int found = 0;

        for (size_t k = at + 1; k < seq.size() && found < count; ++k)
            if (seq[k].event->type != nodeType::rest)
            {
                target = seq[k].event->id;
                ++found;
            }

        if (found < count)
            return fail ("There are not enough notes after it.");

        const auto& root = e.score.root();
        const auto from = e.event().id;
        const bool isHairpin = kind != "slur";
        std::string sameKind;
        std::vector<std::string> others;   // a slur and a hairpin can share a start; two hairpins cannot

        for (const auto& sp : root.children)
        {
            if (sp.type != nodeType::spanner || sp.prop ("from").asString() != from)
                continue;

            if (sp.prop ("kind").asString() == kind)
                sameKind = sp.id;
            else if (isHairpin && sp.prop ("kind").asString() != "slur")
                others.push_back (sp.id);
        }

        undo.beginGroup (kind == "slur" ? "Slur" : "Hairpin");
        bool ok = true;

        if (! sameKind.empty())
        {
            ok = undo.perform (std::make_unique<RemoveNodeCommand> (sameKind, "Slur"));
        }
        else
        {
            for (const auto& id : others)
                ok = ok && undo.perform (std::make_unique<RemoveNodeCommand> (id, "Hairpin"));

            auto node = e.score.makeNode (nodeType::spanner);
            node.props["kind"] = Json (kind);
            node.props["from"] = Json (from);
            node.props["to"] = Json (target);
            ok = ok && undo.perform (std::make_unique<InsertNodeCommand> (root.id, root.children.size(), std::move (node), "Slur"));
        }

        undo.endGroup();

        if (! ok)
        {
            undo.undo();
            return fail ("The change did not fit the score.");
        }

        return success (! sameKind.empty() ? "Taken away." : kind == "slur" ? "Slur added." : kind == "cresc" ? "Crescendo added." : "Diminuendo added.");
    }

    // A tempo mark at the place of the selected note, in the first staff: a number of beats per minute and/or text. With
    // neither, the mark there is taken away.
    EditResult tempoMark (Score& score, UndoManager& undo, const std::string& id, int bpm, std::string text)
    {
        const auto* node = score.find (id);

        if (node != nullptr && node->type == nodeType::note)   // a note of a chord
        {
            const auto* p = score.findParent (id);

            if (p != nullptr && p->type == nodeType::chord)
                node = p;
        }

        const auto* layer = node != nullptr ? score.findParent (node->id) : nullptr;
        const auto* measure = layer != nullptr ? score.findParent (layer->id) : nullptr;
        size_t measureIndex = 0;
        const auto* staff = measure != nullptr ? score.findParent (measure->id, &measureIndex) : nullptr;
        const auto* part = staff != nullptr ? score.findParent (staff->id) : nullptr;

        if (part == nullptr || node == nullptr || ! isEvent (*node) || part->children.empty() || measureIndex >= part->children.front().children.size())
            return fail ("Select a note or a rest first.");

        if (bpm != 0 && (bpm < 20 || bpm > 400))
            return fail ("The tempo can be 20 to 400 beats per minute.");

        while (! text.empty() && text.front() == ' ')
            text.erase (text.begin());

        while (! text.empty() && text.back() == ' ')
            text.pop_back();

        if (text.size() > 40)
            return fail ("The tempo text can be 40 characters long at most.");

        const auto& first = part->children.front().children[measureIndex];   // the measure of the first staff
        const auto onset = node->prop ("onset").asInt();
        auto children = first.children;
        bool had = false;

        children.erase (std::remove_if (children.begin(), children.end(), [&] (const Node& n)
        {
            const bool same = n.type == nodeType::tempo && n.prop ("onset").asInt() == onset;
            had = had || same;
            return same;
        }), children.end());

        if (bpm == 0 && text.empty())
        {
            if (! had)
                return fail ("There is no tempo mark there to take away.");
        }
        else
        {
            auto mark = score.makeNode (nodeType::tempo);
            mark.props["onset"] = Json (onset);

            if (bpm > 0)
                mark.props["bpm"] = Json (bpm);

            if (! text.empty())
                mark.props["text"] = Json (text);

            // marks are kept in order of their place in the bar
            auto at = children.end();

            for (auto it = children.begin(); it != children.end(); ++it)
                if (it->type == nodeType::tempo && it->prop ("onset").asInt() > onset)
                {
                    at = it;
                    break;
                }

            children.insert (at, std::move (mark));
        }

        std::vector<ReplaceChildrenCommand::Change> changes;
        changes.push_back ({ first.id, std::move (children) });

        if (! undo.perform (std::make_unique<ReplaceChildrenCommand> (std::move (changes), "Tempo")))
            return fail ("The change did not fit the score.");

        return success (bpm == 0 && text.empty() ? "Tempo mark taken away." : "Tempo mark set.");
    }

    //==========================================================================
    // Page layout (7d)

    // The part of the score the selected event is in, and the index of its measure.
    const Node* partAndMeasure (const Score& score, const std::string& id, size_t& measureIndex)
    {
        const auto* node = score.find (id);

        if (node != nullptr && node->type == nodeType::note)
        {
            const auto* p = score.findParent (id);

            if (p != nullptr && p->type == nodeType::chord)
                node = p;
        }

        const auto* layer = node != nullptr && isEvent (*node) ? score.findParent (node->id) : nullptr;
        const auto* measure = layer != nullptr ? score.findParent (layer->id) : nullptr;
        const auto* staff = measure != nullptr ? score.findParent (measure->id, &measureIndex) : nullptr;
        return staff != nullptr ? score.findParent (staff->id) : nullptr;
    }

    const Node* firstPart (const Score& score)
    {
        for (const auto& c : score.root().children)
            if (c.type == nodeType::part && ! c.children.empty())
                return &c;

        return nullptr;
    }

    // A line or page break before the measure of the selected event (stored on the measure of the first staff). The
    // same mode again, or "none", takes it away.
    EditResult breakBefore (Score& score, UndoManager& undo, const std::string& id, const std::string& mode)
    {
        if (mode != "system" && mode != "page" && mode != "none")
            return fail ("A break can be a new line, a new page or none.");

        size_t index = 0;
        const auto* part = partAndMeasure (score, id, index);

        if (part == nullptr || part->children.empty() || index >= part->children.front().children.size())
            return fail ("Select a note or a rest first.");

        if (index == 0)
            return fail ("The first measure always starts the first line.");

        const auto& measure = part->children.front().children[index];
        const auto current = measure.prop ("break").asString();
        const auto wanted = mode == current ? std::string ("none") : mode;

        if (wanted == "none" && current.empty())
            return fail ("There is no break before this measure.");

        const bool ok = undo.perform (std::make_unique<SetPropertyCommand> (measure.id, "break", wanted == "none" ? std::nullopt : std::optional<Json> (Json (wanted)), "Break"));

        if (! ok)
            return fail ("The change did not fit the score.");

        return success (wanted == "none" ? "The break is taken away."
                                         : wanted == "page" ? "A new page starts at this measure." : "A new line starts at this measure.");
    }

    // A line break before every count-th measure (the first line holds count measures); 0 gives the choice back to the program.
    // Page breaks stay as they are.
    EditResult barsPerLine (Score& score, UndoManager& undo, int count)
    {
        if (count < 0 || count > 32)
            return fail ("A line can hold 1 to 32 measures.");

        const auto* part = firstPart (score);

        if (part == nullptr)
            return fail ("There is no score to edit.");

        const auto& measures = part->children.front().children;
        undo.beginGroup ("Measures per line");
        bool ok = true, any = false;

        for (size_t i = 1; i < measures.size() && ok; ++i)
        {
            const auto current = measures[i].prop ("break").asString();

            if (current == "page")
                continue;

            const bool wanted = count > 0 && i % (size_t) count == 0;

            if (wanted == (current == "system"))
                continue;

            any = true;
            ok = undo.perform (std::make_unique<SetPropertyCommand> (measures[i].id, "break",
                                                                      wanted ? std::optional<Json> (Json ("system")) : std::nullopt, "Measures per line"));
        }

        undo.endGroup();

        if (! ok)
        {
            undo.undo();
            return fail ("The change did not fit the score.");
        }

        if (! any)
            return fail ("The lines are broken like that already.");

        return success (count == 0 ? "The program breaks the lines." : "A line holds " + std::to_string (count) + (count == 1 ? " measure." : " measures."));
    }

    // The distance between the lines of music (system) and between the staves of one line (staff), in Verovio units.
    EditResult setSpacing (Score& score, UndoManager& undo, const Json& system, const Json& staff)
    {
        auto valid = [] (const Json& v) { return ! v.isNumber() || (v.asInt() >= 4 && v.asInt() <= 40); };

        if (! valid (system) || ! valid (staff))
            return fail ("The spacing can be 4 to 40.");

        if (! system.isNumber() && ! staff.isNumber())
            return fail ("Nothing to set.");

        const Node* layout = nullptr;

        for (const auto& c : score.root().children)
            if (c.type == nodeType::layout)
                layout = &c;

        undo.beginGroup ("Spacing");
        bool ok = true, any = false;

        if (layout == nullptr)
        {
            auto node = score.makeNode (nodeType::layout);

            if (system.isNumber()) node.props["systemSpacing"] = Json (system.asInt());
            if (staff.isNumber())  node.props["staffSpacing"] = Json (staff.asInt());
            ok = undo.perform (std::make_unique<InsertNodeCommand> (score.root().id, score.root().children.size(), std::move (node), "Spacing"));
            any = true;
        }
        else
        {
            if (system.isNumber() && layout->prop ("systemSpacing").asInt() != system.asInt())
            {
                ok = undo.perform (std::make_unique<SetPropertyCommand> (layout->id, "systemSpacing", Json (system.asInt()), "Spacing"));
                any = true;
            }

            if (ok && staff.isNumber() && layout->prop ("staffSpacing").asInt() != staff.asInt())
            {
                ok = undo.perform (std::make_unique<SetPropertyCommand> (layout->id, "staffSpacing", Json (staff.asInt()), "Spacing"));
                any = true;
            }
        }

        undo.endGroup();

        if (! ok)
        {
            undo.undo();
            return fail ("The change did not fit the score.");
        }

        return any ? success ("Spacing changed.") : fail ("The spacing is that already.");
    }

    //==========================================================================
    // Drums: a hit is a note with a drum (the page sends the entry of the drum map).

    struct DrumSpec
    {
        int note = 0;
        std::string name;
        int loc = 5;
        std::string head = "normal";
        int voice = 1;
    };

    bool drumFrom (const Json& j, DrumSpec& d)
    {
        if (! j.isObject() || ! j.get ("note").isNumber() || ! j.get ("loc").isNumber())
            return false;

        d.note = (int) j.get ("note").asInt();
        d.name = j.get ("name").asString();
        d.loc = (int) j.get ("loc").asInt();
        d.head = j.get ("head").asString();
        d.voice = (int) j.get ("voice").asInt (1);

        if (d.head.empty())
            d.head = "normal";

        return d.note >= 0 && d.note <= 127 && d.loc >= -6 && d.loc <= 16 && d.voice >= 1 && d.voice <= 2
               && (d.head == "normal" || d.head == "x" || d.head == "open-x" || d.head == "diamond") && d.name.size() <= 40;
    }

    Node drumNote (Score& score, const DrumSpec& d)
    {
        Node n = score.makeNode (nodeType::note);
        n.props["pitch"] = Json (d.note);
        n.props["drum"] = Json (d.name);
        n.props["loc"] = Json (d.loc);
        n.props["head"] = Json (d.head);
        n.props["vel"] = Json (90);
        return n;
    }

    // Low to high on the staff, as the transcription writes a chord.
    void sortDrumChord (Node& chord)
    {
        std::stable_sort (chord.children.begin(), chord.children.end(), [] (const Node& a, const Node& b)
        {
            return a.prop ("loc").asInt() != b.prop ("loc").asInt() ? a.prop ("loc").asInt() < b.prop ("loc").asInt() : midiOf (a) < midiOf (b);
        });
    }

    bool isPercStaff (const Node& staff)
    {
        return staff.prop ("kind").asString() == "perc" || staff.prop ("clef").asString() == "perc";
    }

    // The end of the beat group a time is in (as written in the measure), at most the end of the measure.
    int64_t groupEndAfter (const Geometry& g, int64_t t)
    {
        int64_t at = 0;

        for (const auto group : beatGroups (g.num, g.den, g.virtualLength))
        {
            at += group;

            if (at - g.offset > t)
                return std::min (at - g.offset, g.length);
        }

        return g.length;
    }

    // Adds a drum at the time of the selected note or rest: in the voice of the drum (hands 1, feet 2), as a note of the
    // chord there if a hit is already there, or in the rest. A new hit is as long as the first note value that fits
    // before the end of its beat.
    EditResult addDrum (Edit& e, const DrumSpec& d)
    {
        const auto t = e.event().prop ("onset").asInt();
        auto& measure = e.measure();
        size_t li = measure.children.size();

        for (size_t l = 0; l < measure.children.size(); ++l)
            if (measure.children[l].type == nodeType::layer && measure.children[l].prop ("n").asInt (1) == d.voice)
                li = l;

        if (li == measure.children.size())
        {
            // the voice is not written in this measure yet: a layer of rest, in order of the voices
            Node layer = e.score.makeNode (nodeType::layer);
            layer.props["n"] = Json (d.voice);
            Node rest = e.score.makeNode (nodeType::rest);
            rest.props["onset"] = Json (0);
            rest.props["ticks"] = Json (e.geometry.length);
            rest.props["measureRest"] = Json (true);
            layer.children.push_back (std::move (rest));

            size_t at = 0;

            for (size_t l = 0; l < measure.children.size(); ++l)
                if (measure.children[l].type == nodeType::layer && measure.children[l].prop ("n").asInt (1) < d.voice)
                    at = l + 1;

            measure.children.insert (measure.children.begin() + (std::ptrdiff_t) at, std::move (layer));
            li = at;
        }

        auto& layer = measure.children[li];
        auto slots = slotsOf (layer, e.geometry);
        size_t k = slots.size();

        for (size_t i = 0; i < slots.size(); ++i)
            if (slots[i].on <= t && t < slots[i].on + slots[i].len)
                k = i;

        if (k == slots.size())
            return fail ("There is no room for a drum there.");

        auto& slot = slots[k];
        auto note = drumNote (e.score, d);

        if (slot.node.type != nodeType::rest)
        {
            if (slot.on != t)
                return fail ("A longer hit of this voice is there. Make it shorter first.");

            auto& event = slot.node;
            note.props["onset"] = event.prop ("onset");

            if (event.type == nodeType::note)
            {
                if (midiOf (event) == d.note)
                    return fail ("That drum is there already.");

                Node chord = e.score.makeNode (nodeType::chord);

                for (const auto* key : eventKeys)
                    if (event.has (key))
                        chord.props[key] = event.prop (key);

                Node old = event;

                for (const auto* key : eventKeys)
                    old.props.erase (key);

                old.props["onset"] = chord.prop ("onset");
                chord.children.push_back (std::move (old));
                chord.children.push_back (note);
                event = std::move (chord);
            }
            else
            {
                for (const auto& n : event.children)
                    if (midiOf (n) == d.note)
                        return fail ("That drum is there already.");

                event.children.push_back (note);
            }

            sortDrumChord (event);

            if (! writeLayer (e.score, layer, e.geometry, std::move (slots)))
                return fail ("The measure could not be written again.");

            return success ("Drum added.", note.id);
        }

        if (inTuplet (slot.node))
            return fail ("A drum cannot be added inside a triplet yet.");

        // the rest: before it, the new hit, after it
        const auto restEnd = slot.on + slot.len;
        const auto room = std::min (restEnd, groupEndAfter (e.geometry, t)) - t;
        const auto pieces = splitLength (t + e.geometry.offset, std::max<int64_t> (room, 1), e.geometry.num, e.geometry.den, e.geometry.virtualLength, {}, false);

        if (pieces.empty())
            return fail ("There is no room for a drum there.");

        note.props["dur"] = Json (pieces.front().dur);

        if (pieces.front().dots > 0)
            note.props["dots"] = Json (pieces.front().dots);

        const auto length = pieces.front().ticks;
        const auto rest = slot;
        std::vector<Slot> out;

        for (size_t i = 0; i < slots.size(); ++i)
        {
            if (i != k)
            {
                out.push_back (slots[i]);
                continue;
            }

            if (rest.on < t)
            {
                Slot before;
                before.on = rest.on;
                before.len = t - rest.on;
                before.node.type = nodeType::rest;
                out.push_back (std::move (before));
            }

            Slot hit;
            hit.on = t;
            hit.len = length;
            hit.node = note;
            out.push_back (std::move (hit));

            if (t + length < restEnd)
            {
                Slot after;
                after.on = t + length;
                after.len = restEnd - t - length;
                after.node.type = nodeType::rest;
                out.push_back (std::move (after));
            }
        }

        if (! writeLayer (e.score, layer, e.geometry, std::move (out)))
            return fail ("The measure could not be written again.");

        return success ("Drum added.", note.id);
    }

    // The selected note becomes another drum (the same voice); a chord: click one of its notes.
    EditResult setDrum (Edit& e, const DrumSpec& d)
    {
        auto& event = e.event();

        if (event.type == nodeType::rest)
            return fail ("Select a hit first (or use Add drum on a rest).");

        auto targets = e.targets();

        if (targets.size() != 1)
            return fail ("A chord has several drums. Click one of its notes a second time to select just that one.");

        int layers = 0;

        for (const auto& c : e.measure().children)
            if (c.type == nodeType::layer)
                ++layers;

        if (layers > 1 && e.layer().prop ("n").asInt (1) != d.voice)
            return fail (d.voice == 1 ? "That drum is played with the hands: delete this hit and add the drum there."
                                      : "That drum is played with the feet: delete this hit and add the drum there.");

        auto& note = *targets.front();

        if (midiOf (note) == d.note)
            return fail ("It is that drum already.");

        if (event.type == nodeType::chord)
            for (const auto& n : event.children)
                if (midiOf (n) == d.note)
                    return fail ("That drum is in the chord already.");

        note.props["pitch"] = Json (d.note);
        note.props["drum"] = Json (d.name);
        note.props["loc"] = Json (d.loc);
        note.props["head"] = Json (d.head);
        note.props.erase ("ghost");

        if (event.type == nodeType::chord)
            sortDrumChord (event);

        return success ("Drum changed.");
    }

    // A ghost note (the note in brackets) on or off.
    EditResult toggleGhost (Edit& e)
    {
        if (e.event().type == nodeType::rest)
            return fail ("Select a hit first.");

        auto targets = e.targets();
        bool all = true;

        for (const auto* n : targets)
            all = all && n->prop ("ghost").asBool();

        for (auto* n : targets)
        {
            if (all)
                n->props.erase ("ghost");
            else
                n->props["ghost"] = Json (true);
        }

        return success (all ? "Ghost note taken away." : "Ghost note.");
    }

    // After an edit of the notation staff of a guitar or bass score: the tab staff gets the same events with strings
    // and frets. A note that stays as it was stays on its string; a new or changed note is placed near the hand.
    bool syncTab (Edit& e, std::vector<ReplaceChildrenCommand::Change>& changes, std::string& problem)
    {
        const auto open = openStringsOf (*e.tab);
        const int strings = (int) open.size();
        const std::string instrument = strings == 4 ? "bass" : "guitar";

        if (e.tab->children.size() != e.measures.size() || e.staff->children.size() != e.measures.size())
        {
            problem = "The tab does not fit the notation.";
            return false;
        }

        auto notesOfEvent = [] (const Node& event)
        {
            std::vector<const Node*> notes;

            if (event.type == nodeType::note)
                notes.push_back (&event);
            else if (event.type == nodeType::chord)
                for (const auto& n : event.children)
                    notes.push_back (&n);

            return notes;
        };

        // where every notation note was played before the edit (read from the old tab, by position)
        std::map<std::string, TabNote> before;

        for (size_t i = 0; i < e.tab->children.size(); ++i)
        {
            const auto& tm = e.tab->children[i];
            const auto& sm = e.staff->children[i];
            std::vector<const Node*> tl, sl;

            for (const auto& c : tm.children) if (c.type == nodeType::layer) tl.push_back (&c);
            for (const auto& c : sm.children) if (c.type == nodeType::layer) sl.push_back (&c);

            for (size_t l = 0; l < std::min (tl.size(), sl.size()); ++l)
            {
                if (tl[l]->children.size() != sl[l]->children.size())
                    continue;

                for (size_t k = 0; k < tl[l]->children.size(); ++k)
                {
                    const auto tn = notesOfEvent (tl[l]->children[k]);
                    const auto sn = notesOfEvent (sl[l]->children[k]);

                    if (tn.size() != sn.size())
                        continue;

                    for (size_t j = 0; j < tn.size(); ++j)
                    {
                        TabNote old;
                        old.pitch = (int) tn[j]->prop ("pitch").asInt();
                        old.string = strings - (int) tn[j]->prop ("course").asInt();
                        old.fret = (int) tn[j]->prop ("fret").asInt();
                        before[sn[j]->id] = old;
                    }
                }
            }
        }

        static const char* const decorations[] = { "stem", "dyn", "artic", "fermata", "text", "textPlace", "beamBreak", "beamJoin",
                                                   "accid", "step", "alter", "oct", "offGrid" };

        auto strip = [] (Node& n)
        {
            for (const auto* key : decorations)
                n.props.erase (key);

            n.id += "-t";
        };

        double reference = -1.0;   // where the hand is: the middle of the fretted notes of the last chord

        for (size_t i = 0; i < e.measures.size(); ++i)
        {
            Node tm = e.tab->children[i];
            tm.children.clear();

            for (const auto& layer : e.measures[i].children)
            {
                if (layer.type != nodeType::layer)
                    continue;

                Node tl;
                tl.type = nodeType::layer;
                tl.id = layer.id + "-t";
                tl.props = layer.props;

                for (const auto& event : layer.children)
                {
                    Node t = event;
                    strip (t);

                    if (event.type == nodeType::rest)
                    {
                        tl.children.push_back (std::move (t));
                        continue;
                    }

                    const auto notes = notesOfEvent (event);
                    std::vector<int> pitches;
                    std::vector<TabNote> keep;

                    for (const auto* n : notes)
                    {
                        const auto pitch = midiOf (*n);
                        pitches.push_back (pitch);
                        const auto old = before.find (n->id);

                        if (old != before.end() && old->second.pitch == pitch && old->second.string >= 0 && old->second.string < strings
                            && old->second.fret == pitch - open[(size_t) old->second.string])
                            keep.push_back (old->second);
                    }

                    std::vector<TabNote> placed = keep.size() == notes.size() ? keep : placeChord (pitches, keep, open, reference);

                    if (placed.size() != notes.size())
                    {
                        problem = std::string (notes.size() == 1 ? "That note" : "Those notes") + " cannot be played on a " + instrument
                                  + " (out of its range, or not possible together with the other notes).";
                        return false;
                    }

                    auto place = [&] (Node& note)
                    {
                        const auto pitch = midiOf (note);

                        for (const auto& p : placed)
                        {
                            if (p.pitch == pitch)
                            {
                                note.props["course"] = Json (strings - p.string);
                                note.props["fret"] = Json (p.fret);
                            }
                        }
                    };

                    if (t.type == nodeType::note)
                    {
                        place (t);
                    }
                    else
                    {
                        for (auto& n : t.children)
                        {
                            strip (n);
                            place (n);
                        }
                    }

                    double low = 1000, high = -1;

                    for (const auto& p : placed)
                    {
                        if (p.fret > 0)
                        {
                            low = std::min (low, (double) p.fret);
                            high = std::max (high, (double) p.fret);
                        }
                    }

                    if (high >= 0)
                        reference = (low + high) / 2.0;

                    tl.children.push_back (std::move (t));
                }

                tm.children.push_back (std::move (tl));
            }

            if (! sameNodes (tm.children, e.tab->children[i].children))
                changes.push_back ({ tm.id, tm.children });
        }

        return true;
    }

    // Moves a note of the tab to the next string up or down: the same pitch, another fret. The notation does not change.
    EditResult moveString (Score& score, UndoManager& undo, const std::string& id, const std::string& direction)
    {
        const auto* node = score.find (id);

        if (node == nullptr)
            return fail ("Click a note in the tab first.");

        if (node->type == nodeType::chord)
            return fail ("Click one note of the chord (click it again) to move it to another string.");

        if (node->type != nodeType::note)
            return fail ("Click a note in the tab first.");

        const auto* parent = score.findParent (id);
        const bool inChord = parent != nullptr && parent->type == nodeType::chord;
        const auto* layer = parent != nullptr ? (inChord ? score.findParent (parent->id) : parent) : nullptr;
        const auto* measure = layer != nullptr ? score.findParent (layer->id) : nullptr;
        const auto* staff = measure != nullptr ? score.findParent (measure->id) : nullptr;

        if (staff == nullptr || staff->type != nodeType::staff || ! isTabStaff (*staff))
            return fail ("Click a note in the tab staff first (the notes of the staff above do not have a string).");

        const auto open = openStringsOf (*staff);
        const int strings = (int) open.size();
        const int current = strings - (int) node->prop ("course").asInt();
        const int target = direction == "up" ? current + 1 : current - 1;

        if (target < 0 || target >= strings)
            return fail (direction == "up" ? "There is no higher string." : "There is no lower string.");

        const int pitch = (int) node->prop ("pitch").asInt();
        const int fret = pitch - open[(size_t) target];

        if (fret < 0 || fret > 22)
            return fail ("That note cannot be played on that string (it would be fret " + std::to_string (fret) + ").");

        if (inChord)
            for (const auto& other : parent->children)
                if (other.id != id && strings - (int) other.prop ("course").asInt() == target)
                    return fail ("Another note of the chord is on that string.");

        Node changed = *measure;

        for (auto& l : changed.children)
        {
            for (auto& event : l.children)
            {
                auto fix = [&] (Node& note)
                {
                    note.props["course"] = Json (strings - target);
                    note.props["fret"] = Json (fret);
                };

                if (event.id == id)
                    fix (event);

                for (auto& n : event.children)
                    if (n.id == id)
                        fix (n);
            }
        }

        std::vector<ReplaceChildrenCommand::Change> changes;
        changes.push_back ({ changed.id, changed.children });

        if (! undo.perform (std::make_unique<ReplaceChildrenCommand> (std::move (changes), "Move to another string")))
            return fail ("The change did not fit the score.");

        return success ("On string " + std::to_string (strings - target) + ", fret " + std::to_string (fret) + ".", id);
    }

    EditResult finish (Edit& e, UndoManager& undo, const std::string& name, EditResult result)
    {
        if (! result.ok)
            return result;

        normaliseTies (e.measures);

        // a voice above the first that is only rest is not written
        for (auto& m : e.measures)
            m.children.erase (std::remove_if (m.children.begin(), m.children.end(), [] (const Node& n)
            {
                return n.type == nodeType::layer && n.prop ("n").asInt (1) > 1 && n.children.size() == 1
                       && n.children.front().type == nodeType::rest && n.children.front().prop ("measureRest").asBool();
            }), m.children.end());

        if (! isPercStaff (*e.staff))
            for (auto& m : e.measures)
                detail::refreshAccidentals (m, e.fifths);

        std::vector<ReplaceChildrenCommand::Change> changes;

        for (size_t i = 0; i < e.measures.size(); ++i)
            if (! sameNodes (e.measures[i].children, e.staff->children[i].children))
                changes.push_back ({ e.measures[i].id, e.measures[i].children });

        if (changes.empty())
            return fail ("Nothing changed.");

        if (e.tab != nullptr)
        {
            std::string problem;

            if (! syncTab (e, changes, problem))
                return fail (problem);
        }

        if (! undo.perform (std::make_unique<ReplaceChildrenCommand> (std::move (changes), name)))
            return fail ("The change did not fit the score.");

        return result;
    }
    // A new key signature for the whole score: every note is spelled for it again (pitches stay), the accidentals
    // are worked out again. One undo step.
    EditResult changeKey (Score& score, UndoManager& undo, int fifths, bool minor)
    {
        if (fifths < -7 || fifths > 7)
            return fail ("A key has at most seven sharps or flats.");

        const Node* part = nullptr;

        for (const auto& c : score.root().children)
            if (c.type == nodeType::part)
                part = &c;

        std::vector<ReplaceChildrenCommand::Change> changes;

        for (const auto& staff : part->children)
        {
            if (isTabStaff (staff))
                continue;   // the tab has no spelling

            auto measures = staff.children;

            for (size_t i = 0; i < measures.size(); ++i)
            {
                std::vector<Node*> stack;

                for (auto& layer : measures[i].children)
                    if (layer.type == nodeType::layer)
                        stack.push_back (&layer);

                while (! stack.empty())
                {
                    auto* n = stack.back();
                    stack.pop_back();

                    if (n->type == nodeType::note)
                        setPitch (*n, midiOf (*n), spell (midiOf (*n), fifths));

                    for (auto& c : n->children)
                        stack.push_back (&c);
                }

                detail::refreshAccidentals (measures[i], fifths);

                if (! sameNodes (measures[i].children, staff.children[i].children))
                    changes.push_back ({ measures[i].id, measures[i].children });
            }
        }

        const auto& root = score.root();
        const int majorTonic = ((fifths * 7) % 12 + 12) % 12;
        const int tonic = minor ? (majorTonic + 9) % 12 : majorTonic;
        const std::string mode = minor ? "minor" : "major";
        const bool same = root.prop ("keyFifths").asInt() == fifths && root.prop ("keyTonic").asInt() == tonic && root.prop ("keyMode").asString() == mode;

        if (changes.empty() && same)
            return fail ("The score is in that key already.");

        undo.beginGroup ("Change key");

        bool ok = true;
        ok = ok && undo.perform (std::make_unique<SetPropertyCommand> (root.id, "keyFifths", Json (fifths), "Change key"));
        ok = ok && undo.perform (std::make_unique<SetPropertyCommand> (root.id, "keyTonic", Json (tonic), "Change key"));
        ok = ok && undo.perform (std::make_unique<SetPropertyCommand> (root.id, "keyMode", Json (mode), "Change key"));

        if (ok && ! changes.empty())
            ok = undo.perform (std::make_unique<ReplaceChildrenCommand> (std::move (changes), "Change key"));

        undo.endGroup();

        if (! ok)
        {
            undo.undo();
            return fail ("The change did not fit the score.");
        }

        return success ("The key is changed.");
    }
}

//==============================================================================
std::string editBlocker
 (const Score& score)
{
    const Node* part = nullptr;

    for (const auto& c : score.root().children)
        if (c.type == nodeType::part)
            part = &c;

    if (part == nullptr || part->children.empty())
        return "There is no score to edit.";

    return {};
}

namespace
{
    EditResult performOn (Score& score, UndoManager& undo, const Json& request);
}

EditResult performEdit (Score& score, UndoManager& undo, const Json& request)
{
    // moving a note of the tab to another string works on the tab itself
    if (request.get ("op").asString() == "string")
    {
        if (const auto blocked = editBlocker (score); ! blocked.empty())
            return fail (blocked);

        return moveString (score, undo, request.get ("id").asString(), request.get ("dir").asString());
    }

    // A click in the tab of a guitar or bass score edits the note of the notation it stands for; the answer then
    // selects the tab note again.
    const auto id = request.get ("id").asString();
    const auto notation = id.empty() ? id : notationIdOf (score, id);

    if (notation == id)
        return performOn (score, undo, request);

    auto changed = request;
    changed.set ("id", notation);
    auto result = performOn (score, undo, changed);

    // (an answer that keeps the selection keeps the tab note, whose id the edit may have renewed)
    if (result.ok)
    {
        const auto wanted = tabIdOf (result.select.empty() ? notation : result.select);

        if (score.find (wanted) != nullptr)
            result.select = wanted;
    }

    return result;
}

namespace
{
EditResult performOn (Score& score, UndoManager& undo, const Json& request)
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

    bool drumScore = false;

    for (const auto& c : score.root().children)
        if (c.type == nodeType::part && ! c.children.empty() && isPercStaff (c.children.front()))
            drumScore = true;

    // what only pitched music has
    static const char* const notForDrums[] = { "pitch", "letter", "interval", "respell", "key", "stem", "voice", "slur", "hairpin" };

    if (drumScore)
        for (const auto* name : notForDrums)
            if (op == name)
                return fail ("That is for pitched music. A drum score has the Drums row: choose a drum and add it or change a hit to it.");

    if (op == "key")
        return changeKey (score, undo, (int) request.get ("fifths").asInt(), request.get ("minor").asBool());

    if (op == "break")
        return breakBefore (score, undo, request.get ("id").asString(), request.get ("mode").asString());

    if (op == "perLine")
        return barsPerLine (score, undo, (int) request.get ("count").asInt());

    if (op == "spacing")
        return setSpacing (score, undo, request.get ("system"), request.get ("staff"));

    if (op == "tempo")
        return tempoMark (score, undo, request.get ("id").asString(), (int) request.get ("bpm").asInt(), request.get ("text").asString());

    Edit e (score);

    if (! locate (e, request.get ("id").asString()))
        return fail ("Select a note or a rest first.");

    if (op == "voice" && e.tab != nullptr)
        return fail ("A guitar or bass score has one voice.");

    if (op == "drumAdd" || op == "drumSet")
    {
        DrumSpec d;

        if (! isPercStaff (*e.staff))
            return fail ("Drums can only be added to a drum score.");

        if (! drumFrom (request.get ("drum"), d))
            return fail ("Choose a drum first.");

        return finish (e, undo, op == "drumAdd" ? "Add drum" : "Change drum", op == "drumAdd" ? addDrum (e, d) : setDrum (e, d));
    }

    if (op == "ghost")
    {
        if (! isPercStaff (*e.staff))
            return fail ("Ghost notes are for drums.");

        return finish (e, undo, "Ghost note", toggleGhost (e));
    }

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

    if (op == "respell")
        return finish (e, undo, "Change spelling", respell (e));

    if (op == "stem")
        return finish (e, undo, "Stem direction", setStem (e, request.get ("dir").asString()));

    if (op == "beam")
        return finish (e, undo, "Beam", setBeam (e, request.get ("mode").asString()));

    if (op == "voice")
        return finish (e, undo, "Change voice", moveToVoice (e, (int) request.get ("voice").asInt()));

    if (op == "dynamic")
        return finish (e, undo, "Dynamic", setDynamic (e, request.get ("value").asString()));

    if (op == "artic")
        return finish (e, undo, "Articulation", setArticulation (e, request.get ("value").asString()));

    if (op == "text")
        return finish (e, undo, "Text", setText (e, request.get ("text").asString(), request.get ("place").asString()));

    if (op == "clear")
        return finish (e, undo, "Take away markings", clearMarks (e));

    if (op == "slur")
        return spanner (e, undo, "slur", (int) request.get ("count").asInt (1));

    if (op == "hairpin")
        return spanner (e, undo, request.get ("form").asString() == "dim" ? "dim" : "cresc", (int) request.get ("count").asInt (1));

    return fail ("Unknown edit \"" + op + "\".");
}
}  // namespace

}  // namespace trs
