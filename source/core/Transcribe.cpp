#include "Notation.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace trs
{

//==============================================================================
// Settings

Json TranscriptionSettings::toJson() const
{
    auto j = Json::object();
    j.set ("grid", grid);
    j.set ("triplets", triplets);
    j.set ("splitPoint", splitPoint);
    j.set ("autoPickup", autoPickup);
    j.set ("keyTonic", keyTonic);
    j.set ("keyMinor", keyMinor);
    return j;
}

TranscriptionSettings TranscriptionSettings::fromJson (const Json& j)
{
    TranscriptionSettings s;
    const auto grid = (int) j.get ("grid").asInt (s.grid);
    s.grid = grid == 4 || grid == 8 || grid == 16 || grid == 32 ? grid : s.grid;
    s.triplets = j.get ("triplets").asBool (s.triplets);
    s.splitPoint = std::max (21, std::min (108, (int) j.get ("splitPoint").asInt (s.splitPoint)));
    s.autoPickup = j.get ("autoPickup").asBool (s.autoPickup);
    const auto tonic = (int) j.get ("keyTonic").asInt (-1);
    s.keyTonic = tonic >= 0 && tonic < 12 ? tonic : -1;
    s.keyMinor = j.get ("keyMinor").asBool (false);
    return s;
}

bool TranscriptionSettings::operator== (const TranscriptionSettings& o) const
{
    return grid == o.grid && triplets == o.triplets && splitPoint == o.splitPoint && autoPickup == o.autoPickup
        && keyTonic == o.keyTonic && keyMinor == o.keyMinor;
}

//==============================================================================
namespace
{
    int64_t nominalLength (const Bar& bar)
    {
        return bar.den > 0 ? (int64_t) bar.num * 4 * ticksPerQuarter / bar.den : 4 * ticksPerQuarter;
    }

    // Sharps (F C G D A E B) or flats (B E A D G C F) in a key signature.
    int keySignatureAlter (char step, int fifths)
    {
        static const char sharps[] = "FCGDAEB";
        static const char flats[] = "BEADGCF";

        if (fifths > 0)
        {
            for (int i = 0; i < fifths && i < 7; ++i)
                if (sharps[i] == step)
                    return 1;
        }
        else if (fifths < 0)
        {
            for (int i = 0; i < -fifths && i < 7; ++i)
                if (flats[i] == step)
                    return -1;
        }

        return 0;
    }

    struct Segment
    {
        int64_t on = 0;         // from the (virtual) bar start
        int64_t length = 0;
        const VoiceEvent* event = nullptr;
        bool tiedFromPrevious = false;
        bool tiedToNext = false;
    };

    struct Built
    {
        std::vector<Node> layers;
    };

    class Builder
    {
    public:
        Builder (Score& s, const std::vector<QNote>& q, const std::vector<SpelledPitch>& sp,
                 const std::vector<std::pair<int64_t, bool>>& tripletBeatStarts, int fifthsInKey)
            : score (s), notes (q), spelled (sp), fifths (fifthsInKey)
        {
            for (const auto& [start, flag] : tripletBeatStarts)
                if (flag)
                    tripletStarts.push_back (start);
        }

        // One measure of one staff.
        Node measure (const Bar& bar, int number, const StaffVoices& staff)
        {
            auto m = score.makeNode (nodeType::measure);
            m.props["n"] = Json (number);
            m.props["num"] = Json (bar.num);
            m.props["den"] = Json (bar.den);
            m.props["startTick"] = Json ((int64_t) bar.start);
            m.props["ticks"] = Json ((int64_t) bar.length);

            if (bar.pickup)
                m.props["pickup"] = Json (true);

            if (bar.irregular)
                m.props["irregular"] = Json (true);

            const auto nominal = nominalLength (bar);
            const int64_t offset = bar.pickup ? nominal - bar.length : 0;
            const auto virtualLength = bar.pickup ? nominal : bar.length;
            const auto barStart = bar.start;
            const auto barEnd = bar.start + bar.length;

            // The notes of every voice that sound in this bar.
            std::vector<std::vector<Segment>> perVoice (staff.voices.size());

            for (size_t v = 0; v < staff.voices.size(); ++v)
            {
                for (const auto& e : staff.voices[v])
                {
                    const auto s = std::max (e.on, barStart);
                    const auto t = std::min (e.on + e.dur, barEnd);

                    if (t <= s)
                        continue;

                    Segment seg;
                    seg.on = s - barStart + offset;
                    seg.length = t - s;
                    seg.event = &e;
                    seg.tiedFromPrevious = e.on < barStart;
                    seg.tiedToNext = e.on + e.dur > barEnd;
                    perVoice[v].push_back (seg);
                }
            }

            const auto triplets = tripletBeatsFor (bar, offset, perVoice);

            for (size_t v = 0; v < std::max<size_t> (1, perVoice.size()); ++v)
            {
                const std::vector<Segment> none;
                const auto& segments = v < perVoice.size() ? perVoice[v] : none;

                if (v > 0 && segments.empty())
                    continue;

                m.children.push_back (layer (bar, (int) v + 1, segments, offset, virtualLength, triplets));
            }

            return m;
        }

        // Shows an accidental where the spelling differs from what the key signature and the earlier
        // notes of the measure already say.
        void addAccidentals (Node& staffMeasure)
        {
            std::vector<Node*> noteNodes;

            std::vector<Node*> stack;

            for (auto& layer : staffMeasure.children)
                if (layer.type == nodeType::layer)
                    stack.push_back (&layer);

            while (! stack.empty())
            {
                auto* n = stack.back();
                stack.pop_back();

                if (n->type == nodeType::note)
                    noteNodes.push_back (n);

                for (auto& c : n->children)
                    stack.push_back (&c);
            }

            std::stable_sort (noteNodes.begin(), noteNodes.end(), [] (const Node* a, const Node* b)
            {
                return a->prop ("onset").asInt() < b->prop ("onset").asInt();
            });

            std::map<std::pair<char, int>, int> state;

            for (auto* n : noteNodes)
            {
                const char step = n->prop ("step").asString()[0];
                const int octave = (int) n->prop ("oct").asInt();
                const int alter = (int) n->prop ("alter").asInt();
                const auto key = std::make_pair (step, octave);
                const auto it = state.find (key);
                const auto current = it != state.end() ? it->second : keySignatureAlter (step, fifths);
                const auto tied = n->prop ("tie").asString() == "t" || n->prop ("tie").asString() == "m";

                if (alter != current && ! tied)
                {
                    const char* shown = alter == 0 ? "n" : alter == 1 ? "s" : alter == -1 ? "f" : alter >= 2 ? "ss" : "ff";
                    n->props["accid"] = Json (shown);
                }

                state[key] = alter;
            }
        }

    private:
        Score& score;
        const std::vector<QNote>& notes;
        const std::vector<SpelledPitch>& spelled;
        int fifths;
        std::vector<int64_t> tripletStarts;   // absolute starts of the triplet beats
        int tupletCounter = 0;
        int beamCounter = 0;

        // Triplet beats in this bar, with the size of their slots: the biggest of 320, 160 and 80
        // that every note boundary inside the beat falls on.
        std::vector<std::pair<int64_t, int>> tripletBeatsFor (const Bar& bar, int64_t offset,
                                                              const std::vector<std::vector<Segment>>& perVoice) const
        {
            std::vector<std::pair<int64_t, int>> out;

            for (const auto start : tripletStarts)
            {
                if (start < bar.start || start >= bar.start + bar.length)
                    continue;

                const auto beatStart = start - bar.start + offset;
                int unit = 320;

                for (const auto& segments : perVoice)
                {
                    for (const auto& seg : segments)
                    {
                        for (const auto boundary : { seg.on, seg.on + seg.length })
                        {
                            const auto rel = boundary - beatStart;

                            if (rel <= 0 || rel >= ticksPerQuarter)
                                continue;

                            while (unit > 80 && rel % unit != 0)
                                unit /= 2;
                        }
                    }
                }

                out.push_back ({ beatStart, unit });
            }

            return out;
        }

        // Boundaries where a new beam group starts, from the bar start.
        std::vector<int64_t> beamBoundaries (const Bar& bar, int64_t offset) const
        {
            const auto nominal = nominalLength (bar);
            const auto groups = beatGroups (bar.num, bar.den, bar.pickup ? nominal : bar.length);
            std::vector<int64_t> out;
            int64_t at = 0;

            for (const auto g : groups)
            {
                if (at - offset > 0)
                    out.push_back (at - offset);

                at += g;
            }

            return out;
        }

        Node layer (const Bar& bar, int voiceNumber, const std::vector<Segment>& segments, int64_t offset,
                    int64_t virtualLength, const std::vector<std::pair<int64_t, int>>& triplets)
        {
            auto l = score.makeNode (nodeType::layer);
            l.props["n"] = Json (voiceNumber);

            if (segments.empty())
            {
                auto r = score.makeNode (nodeType::rest);
                r.props["onset"] = Json (0);
                r.props["ticks"] = Json ((int64_t) bar.length);
                r.props["measureRest"] = Json (true);
                l.children.push_back (std::move (r));
                return l;
            }

            struct Item
            {
                RhythmPiece piece;
                bool rest = false;
                const Segment* segment = nullptr;
            };

            std::vector<Item> items;
            int64_t cursor = offset;   // a pickup bar starts part way into its virtual full bar

            auto addRests = [&] (int64_t from, int64_t to)
            {
                for (auto& p : splitLength (from, to - from, bar.num, bar.den, virtualLength, triplets, true))
                    items.push_back ({ p, true, nullptr });
            };

            for (const auto& seg : segments)
            {
                if (seg.on > cursor)
                    addRests (cursor, seg.on);

                auto pieces = splitLength (seg.on, seg.length, bar.num, bar.den, virtualLength, triplets, false);

                if (! pieces.empty())
                {
                    if (seg.tiedFromPrevious)
                        pieces.front().tiedFromPrevious = true;

                    if (seg.tiedToNext)
                        pieces.back().tiedToNext = true;
                }

                for (auto& p : pieces)
                    items.push_back ({ p, false, &seg });

                cursor = seg.on + seg.length;
            }

            if (cursor < virtualLength)
                addRests (cursor, virtualLength);

            // tuplet groups and beams
            const auto boundaries = beamBoundaries (bar, offset);
            std::string tupletId;
            int64_t tupletStart = -1;
            std::string beamId;
            size_t beamGroup = ~(size_t) 0;
            std::vector<size_t> beamed;   // indexes into l.children

            auto closeBeam = [&]
            {
                if (beamed.size() < 2)
                    for (const auto i : beamed)
                        l.children[i].props.erase ("beam");

                beamed.clear();
                beamId.clear();
            };

            for (auto& item : items)
            {
                const auto& p = item.piece;
                Node event;

                if (item.rest)
                {
                    event = score.makeNode (nodeType::rest);
                }
                else
                {
                    const auto& chord = item.segment->event->notes;

                    if (chord.size() == 1)
                    {
                        event = noteNode (chord.front(), p);
                    }
                    else
                    {
                        event = score.makeNode (nodeType::chord);

                        for (const auto index : chord)
                            event.children.push_back (noteNode (index, p));
                    }
                }

                event.props["onset"] = Json ((int64_t) (p.on - offset));

                for (auto& c : event.children)
                    c.props["onset"] = Json ((int64_t) (p.on - offset));
                event.props["ticks"] = Json ((int64_t) p.ticks);
                event.props["dur"] = Json (p.dur);

                if (p.dots > 0)
                    event.props["dots"] = Json (p.dots);

                if (p.tuplet)
                {
                    if (p.groupStart != tupletStart)
                    {
                        tupletId = "tp" + std::to_string (++tupletCounter);
                        tupletStart = p.groupStart;
                    }

                    event.props["tuplet"] = Json (tupletId);
                    event.props["tupletNum"] = Json (3);
                    event.props["tupletNumbase"] = Json (2);
                }

                // beams: eighths and shorter, not rests, inside one beat group
                const bool beamable = ! item.rest && p.dur >= 8;
                const auto onset = p.on - offset;

                if (beamable)
                {
                    size_t group = 0;

                    for (const auto b : boundaries)
                        if (onset >= b)
                            ++group;

                    if (group != beamGroup || beamId.empty())
                    {
                        closeBeam();
                        beamId = "bm" + std::to_string (++beamCounter);
                        beamGroup = group;
                    }

                    event.props["beam"] = Json (beamId);
                    l.children.push_back (std::move (event));
                    beamed.push_back (l.children.size() - 1);
                    continue;
                }

                closeBeam();
                beamGroup = ~(size_t) 0;
                l.children.push_back (std::move (event));
            }

            closeBeam();

            // A single rest that fills the bar is a measure rest.
            if (l.children.size() == 1 && l.children.front().type == nodeType::rest)
            {
                auto& r = l.children.front();
                r.props.clear();
                r.props["onset"] = Json (0);
                r.props["ticks"] = Json ((int64_t) bar.length);
                r.props["measureRest"] = Json (true);
            }

            return l;
        }

        Node noteNode (int index, const RhythmPiece& p)
        {
            const auto& q = notes[(size_t) index];
            const auto& sp = spelled[(size_t) index];
            auto n = score.makeNode (nodeType::note);
            n.props["pitch"] = Json (q.pitch);
            n.props["step"] = Json (std::string (1, sp.step));
            n.props["alter"] = Json (sp.alter);
            n.props["oct"] = Json (sp.octave);
            n.props["vel"] = Json (q.velocity);

            if (q.offGrid)
                n.props["offGrid"] = Json (true);

            if (p.tiedToNext && p.tiedFromPrevious)
                n.props["tie"] = Json ("m");
            else if (p.tiedToNext)
                n.props["tie"] = Json ("i");
            else if (p.tiedFromPrevious)
                n.props["tie"] = Json ("t");

            return n;
        }
    };

    int64_t toTicks (double ppq)
    {
        return (int64_t) std::llround (ppq * ticksPerQuarter);
    }
}

//==============================================================================
TranscriptionResult transcribePiano (const ResolvedCapture& capture, const TranscriptionSettings& settings)
{
    TranscriptionResult result;
    auto& report = result.report;

    int dropped = 0;
    const auto clean = cleanNotes (capture.notes, &dropped);
    report.mergedNotes = dropped;
    report.notes = (int) clean.size();

    int64_t end = toTicks (capture.lengthPpq);

    for (const auto& n : clean)
        end = std::max (end, toTicks (n.offPpq));

    auto bars = buildBars (capture, end);

    if (settings.autoPickup && ! clean.empty())
        applyPickup (bars, toTicks (clean.front().onPpq));

    auto quantized = quantize (clean, bars, settings);
    report.offGridNotes = quantized.offGrid;
    report.tripletBeats = quantized.tripletBeats;

    // Quantised notes may end a little after the last bar.
    int64_t lastEnd = 0;

    for (const auto& q : quantized.notes)
        lastEnd = std::max (lastEnd, q.on + q.dur);

    while (bars.back().start + bars.back().length < lastEnd)
    {
        Bar next;
        next.start = bars.back().start + bars.back().length;
        next.num = bars.back().num;
        next.den = bars.back().den;
        next.length = nominalLength (next);
        bars.push_back (next);
    }

    // key and spelling
    std::vector<int64_t> onsets;
    std::vector<int> pitches;

    for (const auto& q : quantized.notes)
    {
        onsets.push_back (q.on);
        pitches.push_back (q.pitch);
    }

    if (settings.keyTonic >= 0)
    {
        result.key.tonic = settings.keyTonic;
        result.key.minor = settings.keyMinor;
        result.key.correlation = 1.0;
    }
    else
    {
        result.key = detectKey (quantized.notes);
    }

    // The key signature, and the spelling that starts from the key's tonic. Where the key can be
    // written with sharps or with flats (F sharp / G flat), the spelling with fewer accidentals wins.
    auto spellForKey = [&] (int fifths)
    {
        return spellPitches (onsets, pitches, result.key.tonic, tonicLetterForKey (fifths, result.key.minor));
    };

    auto accidentalsOutsideKey = [] (const std::vector<SpelledPitch>& spelling, int fifths)
    {
        int count = 0;

        for (const auto& sp : spelling)
            count += sp.alter != keySignatureAlter (sp.step, fifths) ? 1 : 0;

        return count;
    };

    const auto sharpFifths = fifthsForKey (result.key.tonic, result.key.minor, false);
    const auto flatFifths = fifthsForKey (result.key.tonic, result.key.minor, true);
    auto spelled = spellForKey (sharpFifths);
    result.key.fifths = sharpFifths;

    if (flatFifths != sharpFifths)
    {
        auto alternative = spellForKey (flatFifths);

        if (accidentalsOutsideKey (alternative, flatFifths) < accidentalsOutsideKey (spelled, sharpFifths))
        {
            spelled = std::move (alternative);
            result.key.fifths = flatFifths;
        }
    }

    // voices
    const auto staves = assignPianoVoices (quantized.notes, settings);

    for (const auto& s : staves)
        report.maxVoices = std::max (report.maxVoices, (int) s.voices.size());

    if (report.maxVoices > 4)
        report.warnings.push_back ("A hand needs " + std::to_string (report.maxVoices) + " voices; more than 4 is hard to read.");

    if (report.offGridNotes > 0)
        report.warnings.push_back (std::to_string (report.offGridNotes) + " notes were far from the grid and were moved to the nearest position.");

    if (report.mergedNotes > 0)
        report.warnings.push_back (std::to_string (report.mergedNotes) + " notes of no length or played twice at once were merged.");

    // the score
    Score& score = result.score;
    Builder builder (score, quantized.notes, spelled, quantized.beats, result.key.fifths);

    auto part = score.makeNode (nodeType::part);
    part.props["name"] = Json ("Piano");

    const auto tempos = tempoMarks (capture);
    int measureNumber = bars.front().pickup ? 0 : 1;

    std::vector<Node> staffNodes;

    for (int s = 0; s < 2; ++s)
    {
        auto staff = score.makeNode (nodeType::staff);
        staff.props["n"] = Json (s + 1);
        staff.props["clef"] = Json (s == 0 ? "G" : "F");

        int number = measureNumber;

        for (const auto& bar : bars)
        {
            auto m = builder.measure (bar, number++, staves[(size_t) s]);
            builder.addAccidentals (m);

            if (s == 0)
            {
                for (const auto& t : tempos)
                {
                    const auto tick = std::max (t.tick, bars.front().start);
                    const auto isLast = &bar == &bars.back();

                    if (tick >= bar.start && (tick < bar.start + bar.length || isLast))
                    {
                        auto node = score.makeNode (nodeType::tempo);
                        node.props["onset"] = Json ((int64_t) (tick - bar.start));

                        if (t.bpm > 0)
                            node.props["bpm"] = Json (t.bpm);

                        if (! t.text.empty())
                            node.props["text"] = Json (t.text);

                        m.children.push_back (std::move (node));
                    }
                }
            }

            staff.children.push_back (std::move (m));
        }

        staffNodes.push_back (std::move (staff));
    }

    part.children = std::move (staffNodes);
    report.measures = (int) bars.size();

    std::map<std::string, Json> rootProps;
    rootProps["tpq"] = Json (ticksPerQuarter);
    rootProps["keyFifths"] = Json (result.key.fifths);
    rootProps["keyTonic"] = Json (result.key.tonic);
    rootProps["keyMode"] = Json (result.key.minor ? "minor" : "major");
    rootProps["generator"] = Json ("transcribePiano");

    std::vector<Node> children;
    children.push_back (std::move (part));
    score.setGeneratedContent (std::move (rootProps), std::move (children));

    result.bars = std::move (bars);
    result.notes = std::move (quantized.notes);
    return result;
}

//==============================================================================
namespace
{
    std::string pitchName (const Node& note)
    {
        std::string s (1, note.prop ("step").asString().empty() ? '?' : note.prop ("step").asString()[0]);
        const int alter = (int) note.prop ("alter").asInt();

        if (alter > 0)
            s += std::string ((size_t) alter, '#');
        else if (alter < 0)
            s += std::string ((size_t) -alter, 'b');

        return s + std::to_string (note.prop ("oct").asInt());
    }

    std::string valueText (const Node& e)
    {
        auto s = "/" + std::to_string (e.prop ("dur").asInt());

        for (int i = 0; i < (int) e.prop ("dots").asInt(); ++i)
            s += ".";

        return s;
    }

    std::string tieText (const Node& e)
    {
        // a chord is tied when its notes are; the first note says
        const Node* n = &e;

        if (e.type == nodeType::chord && ! e.children.empty())
            n = &e.children.front();

        const auto tie = n->prop ("tie").asString();
        return tie == "i" || tie == "m" ? "~" : "";
    }

    std::string accidText (const Node& n)
    {
        return n.has ("accid") ? "!" : "";
    }
}

std::string dumpScore (const Score& score)
{
    std::string out;
    const auto& root = score.root();

    out += "score key=" + std::to_string (root.prop ("keyFifths").asInt()) + " " + root.prop ("keyMode").asString() + "\n";

    for (const auto& part : root.children)
    {
        if (part.type != nodeType::part)
            continue;

        size_t measures = 0;

        for (const auto& staff : part.children)
            measures = std::max (measures, staff.children.size());

        for (size_t i = 0; i < measures; ++i)
        {
            for (const auto& staff : part.children)
            {
                if (i >= staff.children.size())
                    continue;

                const auto& m = staff.children[i];

                if (staff.prop ("n").asInt() == 1)
                {
                    out += "m" + std::to_string (m.prop ("n").asInt()) + " " + std::to_string (m.prop ("num").asInt()) + "/"
                         + std::to_string (m.prop ("den").asInt());

                    if (m.prop ("pickup").asBool())
                        out += " pickup:" + std::to_string (m.prop ("ticks").asInt());

                    if (m.prop ("irregular").asBool())
                        out += " irregular:" + std::to_string (m.prop ("ticks").asInt());

                    out += "\n";

                    for (const auto& c : m.children)
                    {
                        if (c.type != nodeType::tempo)
                            continue;

                        out += "  tempo @" + std::to_string (c.prop ("onset").asInt()) + ":";

                        if (c.has ("text"))
                            out += " " + c.prop ("text").asString();

                        if (c.has ("bpm"))
                            out += " " + std::to_string (c.prop ("bpm").asInt());

                        out += "\n";
                    }
                }

                for (const auto& layer : m.children)
                {
                    if (layer.type != nodeType::layer)
                        continue;

                    out += "  S" + std::to_string (staff.prop ("n").asInt()) + " v" + std::to_string (layer.prop ("n").asInt()) + ":";
                    std::string openTuplet, openBeam;

                    for (const auto& e : layer.children)
                    {
                        const auto tuplet = e.prop ("tuplet").asString();
                        const auto beam = e.prop ("beam").asString();

                        // closing: beam first, then the tuplet that holds it; opening: the other way round
                        if (beam != openBeam && ! openBeam.empty())
                            out += " )";

                        if (tuplet != openTuplet && ! openTuplet.empty())
                            out += " >";

                        if (tuplet != openTuplet && ! tuplet.empty())
                            out += " <";

                        if (beam != openBeam && ! beam.empty())
                            out += " (";

                        openTuplet = tuplet;
                        openBeam = beam;

                        out += " ";

                        if (e.type == nodeType::rest)
                        {
                            out += e.prop ("measureRest").asBool() ? "R" : "r" + valueText (e);
                        }
                        else if (e.type == nodeType::chord)
                        {
                            out += "[";

                            for (size_t k = 0; k < e.children.size(); ++k)
                                out += (k > 0 ? " " : "") + pitchName (e.children[k]) + accidText (e.children[k]);

                            out += "]" + valueText (e) + tieText (e);
                        }
                        else
                        {
                            out += pitchName (e) + accidText (e) + valueText (e) + tieText (e);
                        }
                    }

                    if (! openBeam.empty())
                        out += " )";

                    if (! openTuplet.empty())
                        out += " >";

                    out += "\n";
                }
            }
        }
    }

    return out;
}

}  // namespace trs
