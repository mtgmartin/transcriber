#include "Instruments.h"
#include "Notation.h"
#include "TranscribeInternal.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>

namespace trs
{

using namespace detail;

namespace
{
    constexpr int maxFret = 22;

    struct Candidate
    {
        std::vector<TabNote> notes;
        double own = 0.0;          // what this way of playing the chord costs by itself
        double position = -1.0;    // the middle of the fretted notes; -1 when every note is an open string
    };

    // All the ways to play a chord (its notes sorted from high to low) on distinct strings within a
    // hand span, cheapest first.
    std::vector<Candidate> candidatesFor (const std::vector<int>& pitches, const std::vector<int>& open, const std::vector<TabNote>* keep = nullptr)
    {
        std::vector<Candidate> out;
        std::vector<TabNote> current;
        std::vector<bool> used (open.size(), false);

        std::function<void (size_t)> place = [&] (size_t i)
        {
            if (i == pitches.size())
            {
                int lowest = 1000, highest = -1, fretted = 0, sum = 0, opens = 0;

                for (const auto& n : current)
                {
                    sum += n.fret;

                    if (n.fret == 0)
                    {
                        ++opens;
                        continue;
                    }

                    ++fretted;
                    lowest = std::min (lowest, n.fret);
                    highest = std::max (highest, n.fret);
                }

                const int span = fretted > 1 ? highest - lowest : 0;

                if (span > 4)
                    return;

                Candidate c;
                c.notes = current;
                c.own = 0.6 * span * span + 0.08 * ((double) sum / (double) current.size()) - 0.3 * opens;
                c.position = fretted > 0 ? (double) (lowest + highest) / 2.0 : -1.0;
                out.push_back (std::move (c));
                return;
            }

            // a note that is to stay where it is has only that string
            int only = -1;

            if (keep != nullptr)
                for (const auto& k : *keep)
                    if (k.pitch == pitches[i])
                        only = k.string;

            for (size_t s = 0; s < open.size(); ++s)
            {
                const auto fret = pitches[i] - open[s];

                if (used[s] || fret < 0 || fret > maxFret || (only >= 0 && (int) s != only))
                    continue;

                used[s] = true;
                current.push_back ({ pitches[i], (int) s, fret });
                place (i + 1);
                current.pop_back();
                used[s] = false;
            }
        };

        place (0);

        std::stable_sort (out.begin(), out.end(), [] (const Candidate& a, const Candidate& b) { return a.own < b.own; });

        if (out.size() > 40)
            out.resize (40);

        return out;
    }

    double transitionCost (double fromPosition, const Candidate& to, int fromString, int toString, bool single)
    {
        double cost = 0.0;

        if (fromPosition >= 0.0 && to.position >= 0.0)
        {
            const auto distance = std::abs (to.position - fromPosition);
            cost += 0.6 * distance + (distance > 5.0 ? 0.6 * (distance - 5.0) : 0.0);
        }

        if (single && fromString >= 0)
            cost += 0.15 * std::abs (toString - fromString);

        return cost;
    }
}

// Dynamic programming over the chords: every chord has a few ways to be played; the best path
// keeps the hand where it is, within a span, and prefers low positions and open strings.
std::vector<std::vector<TabNote>> assignTab (const std::vector<std::vector<int>>& chords, const std::vector<int>& open)
{
    std::vector<std::vector<TabNote>> result (chords.size());

    if (open.empty())
        return result;

    struct Step
    {
        std::vector<Candidate> candidates;
        std::vector<double> cost;
        std::vector<int> from;
        std::vector<double> effective;   // the hand position after this candidate on its best path
        std::vector<int> lastString;     // the string of the single note, or -1
    };

    std::vector<Step> steps (chords.size());
    std::vector<std::vector<int>> playable (chords.size());

    for (size_t t = 0; t < chords.size(); ++t)
    {
        // the notes of the chord, high to low; unplayable notes are left out (the highest ones last)
        auto pitches = chords[t];
        std::sort (pitches.begin(), pitches.end(), std::greater<int>());
        pitches.erase (std::unique (pitches.begin(), pitches.end()), pitches.end());

        std::vector<int> reachable;

        for (const auto p : pitches)
            if (p >= open.front() && p <= open.back() + maxFret)
                reachable.push_back (p);

        auto& step = steps[t];

        while (! reachable.empty())
        {
            if (reachable.size() > open.size())
                reachable.pop_back();   // more notes than strings: the lowest go

            step.candidates = candidatesFor (reachable, open);

            if (! step.candidates.empty())
                break;

            reachable.pop_back();       // no way to play them together: drop the lowest note
        }

        playable[t] = reachable;
    }

    int previousStep = -1;

    for (size_t t = 0; t < steps.size(); ++t)
    {
        auto& step = steps[t];

        if (step.candidates.empty())
            continue;

        const auto n = step.candidates.size();
        step.cost.assign (n, 0.0);
        step.from.assign (n, -1);
        step.effective.assign (n, -1.0);
        step.lastString.assign (n, -1);

        for (size_t j = 0; j < n; ++j)
        {
            const auto& c = step.candidates[j];
            const bool single = c.notes.size() == 1;
            step.lastString[j] = single ? c.notes.front().string : -1;

            if (previousStep < 0)
            {
                step.cost[j] = c.own;
                step.effective[j] = c.position;
                continue;
            }

            const auto& prev = steps[(size_t) previousStep];
            double best = std::numeric_limits<double>::max();
            int bestI = 0;

            for (size_t i = 0; i < prev.candidates.size(); ++i)
            {
                const auto cost = prev.cost[i] + transitionCost (prev.effective[i], c, prev.lastString[i], c.notes.front().string, single);

                if (cost < best)
                {
                    best = cost;
                    bestI = (int) i;
                }
            }

            step.cost[j] = best + c.own;
            step.from[j] = bestI;
            step.effective[j] = c.position >= 0.0 ? c.position : prev.effective[(size_t) bestI];
        }

        previousStep = (int) t;
    }

    // walk back from the cheapest last candidate
    int chosen = -1;

    for (int t = (int) steps.size() - 1; t >= 0; --t)
    {
        auto& step = steps[(size_t) t];

        if (step.candidates.empty())
            continue;

        if (chosen < 0)
            chosen = (int) (std::min_element (step.cost.begin(), step.cost.end()) - step.cost.begin());

        result[(size_t) t] = step.candidates[(size_t) chosen].notes;
        chosen = step.from[(size_t) chosen];   // an index into the previous step that had candidates
    }

    return result;
}

std::vector<TabNote> placeChord (std::vector<int> pitches, const std::vector<TabNote>& keep, const std::vector<int>& open, double reference)
{
    if (open.empty() || pitches.empty())
        return {};

    std::sort (pitches.begin(), pitches.end(), std::greater<int>());
    pitches.erase (std::unique (pitches.begin(), pitches.end()), pitches.end());

    auto candidates = candidatesFor (pitches, open, keep.empty() ? nullptr : &keep);

    if (candidates.empty() && ! keep.empty())
        candidates = candidatesFor (pitches, open);   // the notes that stayed cannot stay: place them again

    if (candidates.empty())
        return {};

    // the cheapest way, counting how far the hand has to move from where it was
    size_t best = 0;
    double bestCost = 1e9;

    for (size_t i = 0; i < candidates.size(); ++i)
    {
        double cost = candidates[i].own;

        if (reference >= 0.0 && candidates[i].position >= 0.0)
            cost += 0.6 * std::abs (candidates[i].position - reference);

        if (cost < bestCost)
        {
            bestCost = cost;
            best = i;
        }
    }

    auto notes = candidates[best].notes;
    std::sort (notes.begin(), notes.end(), [] (const TabNote& a, const TabNote& b) { return a.pitch < b.pitch; });
    return notes;
}

// The tab staff has the same events as the notation staff. Every tab node gets the id of its notation node plus "-t", so
// the ids stay the same when an edit writes the tab again (and a selection survives it).
static void deriveTabIds (const Node& notation, Node& tab)
{
    for (size_t i = 0; i < tab.children.size() && i < notation.children.size(); ++i)
    {
        std::vector<const Node*> nl;
        std::vector<Node*> tl;

        for (const auto& c : notation.children[i].children) if (c.type == nodeType::layer) nl.push_back (&c);
        for (auto& c : tab.children[i].children) if (c.type == nodeType::layer) tl.push_back (&c);

        for (size_t l = 0; l < nl.size() && l < tl.size(); ++l)
        {
            tl[l]->id = nl[l]->id + "-t";

            for (size_t k = 0; k < nl[l]->children.size() && k < tl[l]->children.size(); ++k)
            {
                const auto& n = nl[l]->children[k];
                auto& t = tl[l]->children[k];
                t.id = n.id + "-t";

                for (size_t j = 0; j < n.children.size() && j < t.children.size(); ++j)
                    t.children[j].id = n.children[j].id + "-t";
            }
        }
    }
}

//==============================================================================
TranscriptionResult transcribeFretted (
const ResolvedCapture& capture, const TranscriptionSettings& settings, InstrumentType type)
{
    TranscriptionResult result;
    auto& report = result.report;

    auto prepared = prepareNotes (capture, settings, result);
    auto& quantized = prepared.quantized;
    auto& bars = prepared.bars;
    const auto& notes = quantized.notes;

    // The tuning: the one the user chose (if it can be the strings of this instrument), or the standard one unless the take
    // has notes that cannot be played in it, and then the smallest change that can.
    std::vector<int> open = openStrings (type);

    {
        std::vector<int> pitches;

        for (const auto& q : notes)
            pitches.push_back (q.pitch);

        const auto chosen = tuningFromText (settings.tuning);

        if (settings.tuning != "auto" && validTuning (type, chosen))
        {
            open = chosen;
        }
        else
        {
            open = chooseTuning (type, pitches, maxFret);

            if (open != openStrings (type))
                report.warnings.push_back ("Tuned to " + tuningName (type, open) + " so that all notes can be played.");
        }

        report.tuning = tuningName (type, open);
    }

    const auto spelled = analyseKey (notes, settings, result);

    // One voice: the notes that start together form a chord; it lasts until the next chord, or as
    // long as its longest note if that is shorter (the rest is silence).
    std::vector<std::vector<int>> groups;   // note indexes per onset
    std::vector<int64_t> onsets;

    for (size_t i = 0; i < notes.size(); ++i)
    {
        if (onsets.empty() || onsets.back() != notes[i].on)
        {
            onsets.push_back (notes[i].on);
            groups.emplace_back();
        }

        groups.back().push_back ((int) i);
    }

    std::vector<std::vector<int>> pitchGroups (groups.size());

    for (size_t g = 0; g < groups.size(); ++g)
        for (const auto i : groups[g])
            pitchGroups[g].push_back (notes[(size_t) i].pitch);

    const auto tab = assignTab (pitchGroups, open);

    // which note went to which string and fret; the notes that could not be played are left out
    std::map<int, TabNote> placed;
    int notPlayable = 0;
    int merged = 0;
    StaffVoices staff;
    staff.voices.emplace_back();

    for (size_t g = 0; g < groups.size(); ++g)
    {
        std::vector<int> members;
        std::map<int, bool> pitchSeen;

        for (const auto i : groups[g])
        {
            const auto pitch = notes[(size_t) i].pitch;

            if (pitchSeen[pitch])
            {
                ++merged;
                continue;
            }

            pitchSeen[pitch] = true;
            const auto it = std::find_if (tab[g].begin(), tab[g].end(), [pitch] (const TabNote& n) { return n.pitch == pitch; });

            if (it == tab[g].end())
            {
                ++notPlayable;
                continue;
            }

            placed[i] = *it;
            members.push_back (i);
        }

        if (members.empty())
            continue;

        int64_t longest = 0;

        for (const auto i : members)
            longest = std::max (longest, notes[(size_t) i].dur);

        const auto until = g + 1 < onsets.size() ? onsets[g + 1] - onsets[g] : longest;

        VoiceEvent e;
        e.on = onsets[g];
        e.dur = std::max<int64_t> (1, std::min (longest, until));
        e.notes = members;

        // low to high, as in MEI
        std::sort (e.notes.begin(), e.notes.end(), [&] (int a, int b) { return notes[(size_t) a].pitch < notes[(size_t) b].pitch; });
        staff.voices.front().push_back (std::move (e));
    }

    report.mergedNotes += merged;
    report.maxVoices = 1;

    if (notPlayable > 0)
    {
        const auto low = open.front();
        const auto high = open.back() + maxFret;
        report.warnings.push_back ((notPlayable == 1 ? "1 note cannot" : std::to_string (notPlayable) + " notes cannot") + std::string (" be played on a ")
                                   + std::to_string (open.size()) + "-string " + instrumentName (type) + " (MIDI notes " + std::to_string (low) + "-"
                                   + std::to_string (high) + ", or too many at once) and " + (notPlayable == 1 ? "was" : "were") + " left out.");
    }

    addCommonWarnings (report);

    // the score: standard notation, then the tablature
    Score& score = result.score;
    const auto tempos = tempoMarks (capture);
    const int firstNumber = bars.front().pickup ? 0 : 1;

    auto part = score.makeNode (nodeType::part);
    part.props["name"] = Json (type == InstrumentType::bass ? "Bass" : "Guitar");

    {
        Builder builder (score, notes, spelled, quantized.beats, result.key.fifths);
        auto staffNode = score.makeNode (nodeType::staff);
        staffNode.props["n"] = Json (1);
        staffNode.props["clef"] = Json (type == InstrumentType::bass ? "F8" : "G8");

        int number = firstNumber;

        for (const auto& bar : bars)
        {
            auto m = builder.measure (bar, number++, staff);
            builder.addAccidentals (m);
            addTempoMarks (score, m, bar, bars, tempos);
            staffNode.children.push_back (std::move (m));
        }

        part.children.push_back (std::move (staffNode));
    }

    {
        Builder builder (score, notes, spelled, quantized.beats, result.key.fifths);
        const int strings = (int) open.size();

        builder.noteMaker = [&] (int index, const RhythmPiece& p)
        {
            const auto& q = notes[(size_t) index];
            const auto& t = placed.at (index);
            auto n = score.makeNode (nodeType::note);
            n.props["pitch"] = Json (q.pitch);
            n.props["course"] = Json (strings - t.string);   // course 1 is the highest string
            n.props["fret"] = Json (t.fret);
            n.props["vel"] = Json (q.velocity);

            if (p.tiedToNext && p.tiedFromPrevious)
                n.props["tie"] = Json ("m");
            else if (p.tiedToNext)
                n.props["tie"] = Json ("i");
            else if (p.tiedFromPrevious)
                n.props["tie"] = Json ("t");

            return n;
        };

        auto staffNode = score.makeNode (nodeType::staff);
        staffNode.props["n"] = Json (2);
        staffNode.props["clef"] = Json ("TAB");
        staffNode.props["kind"] = Json ("tab");
        staffNode.props["strings"] = Json (strings);
        staffNode.props["tuning"] = Json (tuningText (open));

        int number = firstNumber;

        for (const auto& bar : bars)
            staffNode.children.push_back (builder.measure (bar, number++, staff));

        part.children.push_back (std::move (staffNode));
    }

    deriveTabIds (part.children[0], part.children[1]);

    finishScore (result, std::move (part), type == InstrumentType::bass ? "transcribeBass" : "transcribeGuitar",
                 std::move (bars), notes, true);
    return result;
}

TranscriptionResult transcribe (const ResolvedCapture& capture, const TranscriptionSettings& settings, const Profile& profile)
{
    switch (profile.type)
    {
        case InstrumentType::drums:  return transcribeDrums (capture, settings, profile.drumMap);
        case InstrumentType::guitar: return transcribeFretted (capture, settings, InstrumentType::guitar);
        case InstrumentType::bass:   return transcribeFretted (capture, settings, InstrumentType::bass);
        default:                     return transcribePiano (capture, settings);
    }
}

}  // namespace trs
