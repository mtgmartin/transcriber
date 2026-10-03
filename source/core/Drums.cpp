#include "Instruments.h"
#include "Notation.h"
#include "TranscribeInternal.h"

#include <algorithm>
#include <map>
#include <set>

namespace trs
{

using namespace detail;

// Drums are written on one five-line staff with a percussion clef. A note's place and notehead come
// from the drum map; hands (voice 1, stems up) and feet (voice 2, stems down) are separate voices.
// A hit is written as long as the time to the next hit of its voice, but not across the end of its beat,
// so a hi-hat pattern reads as eighths and a kick on beats 1 and 3 as quarters with rests between.
TranscriptionResult transcribeDrums (const ResolvedCapture& capture, const TranscriptionSettings& settings, const DrumMap& map)
{
    TranscriptionResult result;
    auto& report = result.report;

    auto prepared = prepareNotes (capture, settings, result);
    auto& quantized = prepared.quantized;
    auto& bars = prepared.bars;
    const auto& notes = quantized.notes;

    // which drum each note is; notes the map does not know are reported, never dropped silently
    std::vector<const DrumEntry*> entryOf (notes.size(), nullptr);
    std::map<int, int> unmapped;

    for (size_t i = 0; i < notes.size(); ++i)
    {
        entryOf[i] = map.find (notes[i].pitch);

        if (entryOf[i] == nullptr)
            ++unmapped[notes[i].pitch];
    }

    // hits of one voice: by onset, one note per drum
    struct Hit { int64_t on; std::vector<int> notes; };
    std::map<int, std::map<int64_t, Hit>> byVoice;   // voice -> onset -> hit
    int merged = 0;

    for (size_t i = 0; i < notes.size(); ++i)
    {
        if (entryOf[i] == nullptr)
            continue;

        auto& hit = byVoice[entryOf[i]->voice][notes[i].on];
        hit.on = notes[i].on;

        // the same drum twice in one slot (a flam or a double hit played inside it) is written once
        bool duplicate = false;

        for (auto& other : hit.notes)
        {
            if (notes[(size_t) other].pitch == notes[i].pitch)
            {
                duplicate = true;

                if (notes[i].velocity > notes[(size_t) other].velocity)
                    other = (int) i;
            }
        }

        if (duplicate)
            ++merged;
        else
            hit.notes.push_back ((int) i);
    }

    report.mergedNotes += merged;

    // The end of the beat group (or the bar) a tick is in: a hit is not written across it.
    auto groupEndAfter = [&] (int64_t tick)
    {
        for (const auto& b : bars)
        {
            if (tick < b.start || tick >= b.start + b.length)
                continue;

            const auto nominal = nominalLength (b);
            const int64_t offset = b.pickup ? nominal - b.length : 0;
            int64_t at = b.start - offset;

            for (const auto g : beatGroups (b.num, b.den, b.pickup ? nominal : b.length))
            {
                at += g;

                if (at > tick)
                    return std::min (at, b.start + b.length);
            }

            return b.start + b.length;
        }

        return tick + ticksPerQuarter;
    };

    StaffVoices staff;

    for (auto& [voice, hits] : byVoice)
    {
        std::vector<VoiceEvent> events;

        for (auto it = hits.begin(); it != hits.end(); ++it)
        {
            const auto next = std::next (it);
            auto length = groupEndAfter (it->first) - it->first;

            if (next != hits.end())
                length = std::min (length, next->first - it->first);

            VoiceEvent e;
            e.on = it->first;
            e.dur = std::max<int64_t> (length, 1);
            e.notes = it->second.notes;

            // low to high on the staff, as in MEI
            std::sort (e.notes.begin(), e.notes.end(), [&] (int a, int b)
            {
                return entryOf[(size_t) a]->loc < entryOf[(size_t) b]->loc;
            });

            events.push_back (std::move (e));
        }

        staff.voices.push_back (std::move (events));
    }

    // The hands are voice 1 and the feet voice 2; a part with only feet (a kick drum track) is one voice.
    report.maxVoices = (int) staff.voices.size();

    if (! unmapped.empty())
    {
        std::string list;
        int count = 0, shown = 0;

        for (const auto& [note, n] : unmapped)
        {
            count += n;

            if (shown++ < 6)
                list += (list.empty() ? "" : ", ") + std::to_string (note);
        }

        if (unmapped.size() > 6)
            list += ", ...";

        report.warnings.push_back ((count == 1 ? "1 note is" : std::to_string (count) + " notes are") + std::string (" not in the drum map (MIDI note ") + list
                                   + (count == 1 ? ") and was left out. Add it to the map to write it." : ") and were left out. Add them to the map to write them."));
    }

    addCommonWarnings (report);

    // the score
    Score& score = result.score;
    static const std::vector<SpelledPitch> noSpelling;
    Builder builder (score, notes, noSpelling, quantized.beats, 0);

    builder.noteMaker = [&] (int index, const RhythmPiece& p)
    {
        const auto& q = notes[(size_t) index];
        const auto& entry = *entryOf[(size_t) index];
        auto n = score.makeNode (nodeType::note);
        n.props["pitch"] = Json (q.pitch);
        n.props["drum"] = Json (entry.name);
        n.props["loc"] = Json (entry.loc);
        n.props["head"] = Json (entry.head);
        n.props["vel"] = Json (q.velocity);

        if (entry.ghostBelow > 0 && q.velocity < entry.ghostBelow)
            n.props["ghost"] = Json (true);

        if (q.offGrid)
            n.props["offGrid"] = Json (true);

        if (p.tiedToNext && p.tiedFromPrevious)
            n.props["tie"] = Json ("m");
        else if (p.tiedToNext)
            n.props["tie"] = Json ("i");
        else if (p.tiedFromPrevious)
            n.props["tie"] = Json ("t");

        return n;
    };

    auto part = score.makeNode (nodeType::part);
    part.props["name"] = Json ("Drums");

    auto staffNode = score.makeNode (nodeType::staff);
    staffNode.props["n"] = Json (1);
    staffNode.props["clef"] = Json ("perc");
    staffNode.props["kind"] = Json ("perc");

    const auto tempos = tempoMarks (capture);
    int number = bars.front().pickup ? 0 : 1;

    for (const auto& bar : bars)
    {
        auto m = builder.measure (bar, number++, staff);
        addTempoMarks (score, m, bar, bars, tempos);
        staffNode.children.push_back (std::move (m));
    }

    part.children.push_back (std::move (staffNode));
    finishScore (result, std::move (part), "transcribeDrums", std::move (bars), notes, false);
    return result;
}

}  // namespace trs
