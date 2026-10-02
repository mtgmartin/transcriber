#include "Notation.h"

#include <algorithm>
#include <cstdlib>
#include <map>

namespace trs
{

namespace
{
    // How far from the split point a note may be and still follow the hand that is already
    // playing nearby (semitones below / above the split point).
    constexpr int zoneBelow = 3;
    constexpr int zoneAbove = 2;

    // How long a hand counts as "just played" for the continuity rule, and how far apart notes
    // may be for the same hand to reach them.
    constexpr int64_t recentTicks = 2 * ticksPerQuarter;
    constexpr int reach = 9;

    struct Recent
    {
        bool any = false;
        int pitch = 0;
        int64_t on = 0;
        int64_t end = 0;
    };

    std::vector<VoiceEvent> chordsOf (const std::vector<QNote>& notes, const std::vector<size_t>& members)
    {
        // Notes that start together and last equally long are one chord.
        std::map<std::pair<int64_t, int64_t>, VoiceEvent> chords;

        for (const auto index : members)
        {
            const auto key = std::make_pair (notes[index].on, notes[index].dur);
            auto& chord = chords[key];
            chord.on = notes[index].on;
            chord.dur = notes[index].dur;
            chord.notes.push_back ((int) index);
        }

        std::vector<VoiceEvent> out;

        for (auto& [key, chord] : chords)
        {
            std::sort (chord.notes.begin(), chord.notes.end(), [&] (int a, int b)
            {
                return notes[(size_t) a].pitch < notes[(size_t) b].pitch;   // low to high, as in MEI
            });

            out.push_back (std::move (chord));
        }

        // by onset; at the same moment the chord with the highest note first
        std::stable_sort (out.begin(), out.end(), [&] (const VoiceEvent& a, const VoiceEvent& b)
        {
            if (a.on != b.on)
                return a.on < b.on;

            return notes[(size_t) a.notes.back()].pitch > notes[(size_t) b.notes.back()].pitch;
        });

        return out;
    }
}

std::vector<StaffVoices> assignPianoVoices (const std::vector<QNote>& notes, const TranscriptionSettings& settings)
{
    // Which hand plays each note: by pitch, except near the split point where the hand that is
    // already playing nearby keeps the note.
    std::vector<int> staff (notes.size(), 0);   // 0 = right hand, 1 = left hand
    Recent recent[2];

    for (size_t i = 0; i < notes.size(); ++i)
    {
        const auto& n = notes[i];
        int hand = n.pitch >= settings.splitPoint ? 0 : 1;

        const auto ambiguous = n.pitch >= settings.splitPoint - zoneBelow && n.pitch <= settings.splitPoint + zoneAbove;

        if (ambiguous)
        {
            int distance[2] = { 1000, 1000 };

            for (int h = 0; h < 2; ++h)
                if (recent[h].any && n.on - recent[h].end <= recentTicks && std::abs (recent[h].pitch - n.pitch) <= reach)
                    distance[h] = std::abs (recent[h].pitch - n.pitch);

            if (distance[0] != distance[1])
                hand = distance[0] < distance[1] ? 0 : 1;
        }

        staff[i] = hand;
        recent[hand] = { true, n.pitch, n.on, n.on + n.dur };
    }

    std::vector<StaffVoices> result (2);

    for (int h = 0; h < 2; ++h)
    {
        std::vector<size_t> members;

        for (size_t i = 0; i < notes.size(); ++i)
            if (staff[i] == h)
                members.push_back (i);

        // Each chord goes into the first voice that is free when it starts.
        std::vector<int64_t> voiceEnd;

        for (auto& chord : chordsOf (notes, members))
        {
            size_t v = 0;

            while (v < voiceEnd.size() && voiceEnd[v] > chord.on)
                ++v;

            if (v == voiceEnd.size())
            {
                voiceEnd.push_back (0);
                result[(size_t) h].voices.emplace_back();
            }

            voiceEnd[v] = chord.on + chord.dur;
            result[(size_t) h].voices[v].push_back (std::move (chord));
        }
    }

    return result;
}

}  // namespace trs
