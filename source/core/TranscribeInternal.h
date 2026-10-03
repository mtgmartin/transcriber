#pragma once

// Shared parts of the transcription pipelines (piano, drums, guitar): the Builder that turns the
// voices of a staff into measures, layers, beams and tuplets.

#include "Notation.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>

namespace trs
{
namespace detail
{
    inline int64_t nominalLength (const Bar& bar)
    {
        return bar.den > 0 ? (int64_t) bar.num * 4 * ticksPerQuarter / bar.den : 4 * ticksPerQuarter;
    }

    // Sharps (F C G D A E B) or flats (B E A D G C F) in a key signature.
    inline int keySignatureAlter (char step, int fifths)
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

    // Sets the accidental to show on every note of one measure of a staff (all its layers): where the
    // spelling differs from what the key signature and the earlier notes of the measure say, and not
    // on a note that continues a tie. The editor calls this again after every change.
    inline void refreshAccidentals (Node& staffMeasure, int fifths)
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

        for (auto* n : noteNodes)
            n->props.erase ("accid");

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
        void addAccidentals (Node& staffMeasure) { refreshAccidentals (staffMeasure, fifths); }

        // Makes the node of one note of a voice event. The default writes a pitched note; drums and
        // tablature set their own.
        std::function<Node (int index, const RhythmPiece&)> noteMaker;

    private:
        Node makeNote (int index, const RhythmPiece& p) { return noteMaker ? noteMaker (index, p) : noteNode (index, p); }

        Score& score;
        const std::vector<QNote>& notes;
        const std::vector<SpelledPitch>& spelled;
        int fifths;
        std::vector<int64_t> tripletStarts;   // absolute starts of the triplet beats
        int tupletCounter = 0;
        int beamCounter = 0;

        // Triplet beats in this bar, with the size of their slots: the biggest of 320, 160, 80, 40 and 20
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

                            while (unit > 20 && rel % unit != 0)
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
                        event = makeNote (chord.front(), p);
                    }
                    else
                    {
                        event = score.makeNode (nodeType::chord);

                        for (const auto index : chord)
                            event.children.push_back (makeNote (index, p));
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

    inline int64_t toTicks (double ppq)
    {
        return (int64_t) std::llround (ppq * ticksPerQuarter);
    }

    //==========================================================================
    // The steps every pipeline starts with.

    struct Prepared
    {
        std::vector<ResolvedNote> clean;
        std::vector<Bar> bars;
        QuantizeResult quantized;
    };

    // Clean-up, bars (with a pickup), quantisation. Fills the counts of the report. The bars are
    // extended when a quantised note runs past the last one.
    inline Prepared prepareNotes (const ResolvedCapture& capture, const TranscriptionSettings& settings, TranscriptionResult& result)
    {
        Prepared p;
        auto& report = result.report;

        int dropped = 0;
        p.clean = cleanNotes (capture.notes, &dropped);
        report.mergedNotes = dropped;
        report.notes = (int) p.clean.size();

        int64_t end = toTicks (capture.lengthPpq);

        for (const auto& n : p.clean)
            end = std::max (end, toTicks (n.offPpq));

        p.bars = buildBars (capture, end);

        if (settings.autoPickup && ! p.clean.empty())
            applyPickup (p.bars, toTicks (p.clean.front().onPpq));

        p.quantized = quantize (p.clean, p.bars, settings);
        // the same note landing in one slot twice becomes one note
        report.mergedNotes += std::max<int> (0, (int) p.clean.size() - (int) p.quantized.notes.size());
        report.offGridNotes = p.quantized.offGrid;
        report.tripletBeats = p.quantized.tripletBeats;
        report.grid = settings.grid;
        report.shortNotes = (int) std::count_if (p.quantized.notes.begin(), p.quantized.notes.end(), [] (const QNote& q) { return q.dur < ticksPerQuarter / 8; });

        int64_t lastEnd = 0;

        for (const auto& q : p.quantized.notes)
            lastEnd = std::max (lastEnd, q.on + q.dur);

        while (p.bars.back().start + p.bars.back().length < lastEnd)
        {
            Bar next;
            next.start = p.bars.back().start + p.bars.back().length;
            next.num = p.bars.back().num;
            next.den = p.bars.back().den;
            next.length = nominalLength (next);
            p.bars.push_back (next);
        }

        return p;
    }

    // The key (detected, or the one the user chose), its signature and the spelling of every note.
    // Where a key can be written with sharps or flats (F sharp / G flat), the spelling with fewer
    // accidentals wins.
    inline std::vector<SpelledPitch> analyseKey (const std::vector<QNote>& notes, const TranscriptionSettings& settings,
                                                 TranscriptionResult& result)
    {
        std::vector<int64_t> onsets;
        std::vector<int> pitches;

        for (const auto& q : notes)
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
            result.key = detectKey (notes);
        }

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

        return spelled;
    }

    // Tempo marks go into the measures of the first staff.
    inline void addTempoMarks (Score& score, Node& measure, const Bar& bar, const std::vector<Bar>& bars, const std::vector<TempoMark>& tempos)
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

                measure.children.push_back (std::move (node));
            }
        }
    }

    inline void addCommonWarnings (TranscriptionReport& report)
    {
        if (report.offGridNotes > 0)
            report.warnings.push_back (report.offGridNotes == 1 ? "1 note was far from the grid and was moved to the nearest position."
                                                                  : std::to_string (report.offGridNotes) + " notes were far from the grid and were moved to the nearest position.");

        // a grid finer than a 32nd keeps the small differences of a human player as tiny notes
        if (report.grid >= 64 && report.shortNotes >= 4 && report.shortNotes * 10 >= report.notes)
            report.warnings.push_back (std::to_string (report.shortNotes) + " notes are shorter than a 32nd note: is the grid of 1/" + std::to_string (report.grid) + " too fine for this playing?");

        if (report.mergedNotes > 0)
            report.warnings.push_back (report.mergedNotes == 1 ? "1 note of no length or played twice at once was merged."
                                                                  : std::to_string (report.mergedNotes) + " notes of no length or played twice at once were merged.");
    }

    // Puts the part into the score and fills the result.
    inline void finishScore (TranscriptionResult& result, Node part, const char* generator, std::vector<Bar> bars,
                             std::vector<QNote> notes, bool pitched)
    {
        auto& score = result.score;
        result.report.measures = (int) bars.size();

        std::map<std::string, Json> rootProps;
        rootProps["tpq"] = Json (ticksPerQuarter);
        rootProps["keyFifths"] = Json (pitched ? result.key.fifths : 0);
        rootProps["keyTonic"] = Json (pitched ? result.key.tonic : 0);
        rootProps["keyMode"] = Json (pitched && result.key.minor ? "minor" : "major");
        rootProps["generator"] = Json (generator);

        std::vector<Node> children;
        children.push_back (std::move (part));
        score.setGeneratedContent (std::move (rootProps), std::move (children));

        result.bars = std::move (bars);
        result.notes = std::move (notes);
    }

}  // namespace detail
}  // namespace trs
