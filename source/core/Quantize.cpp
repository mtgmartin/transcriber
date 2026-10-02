#include "Notation.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace trs
{

namespace
{
    constexpr int64_t beatTicks = ticksPerQuarter;

    int64_t toTicks (double ppq)
    {
        return (int64_t) std::llround (ppq * ticksPerQuarter);
    }

    // Floor division that also works for negative numbers.
    int64_t floorDiv (int64_t a, int64_t b)
    {
        auto q = a / b;
        return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
    }

    int64_t roundToSlot (int64_t x, int64_t slot)
    {
        // nearest multiple, halves go up
        return floorDiv (x + slot / 2, slot) * slot;
    }

    int64_t nominalLength (int num, int den)
    {
        return den > 0 ? (int64_t) num * 4 * ticksPerQuarter / den : 4 * ticksPerQuarter;
    }
}

int straightSlot (const TranscriptionSettings& s)
{
    const auto grid = s.grid >= 32 ? 32 : s.grid >= 16 ? 16 : s.grid >= 8 ? 8 : 4;
    return 4 * ticksPerQuarter / grid;
}

int tripletSlot (const TranscriptionSettings& s)
{
    // A quarter-note grid has no useful triplet; every finer grid has the triplet of its own size.
    return s.triplets && s.grid >= 8 ? straightSlot (s) * 2 / 3 : 0;
}

//==============================================================================
std::vector<ResolvedNote> cleanNotes (const std::vector<ResolvedNote>& in, int* dropped)
{
    std::vector<ResolvedNote> out;
    int count = 0;

    for (const auto& n : in)
    {
        if (n.offPpq - n.onPpq <= 1.0e-6)
        {
            ++count;
            continue;
        }

        out.push_back (n);
    }

    std::stable_sort (out.begin(), out.end(), [] (const ResolvedNote& a, const ResolvedNote& b)
    {
        return a.onPpq != b.onPpq ? a.onPpq < b.onPpq : a.pitch < b.pitch;
    });

    // The same note starting twice at the same moment is one note (keep the longer).
    std::vector<ResolvedNote> unique;

    for (const auto& n : out)
    {
        bool merged = false;

        for (auto it = unique.rbegin(); it != unique.rend() && n.onPpq - it->onPpq < 1.0e-4; ++it)
        {
            if (it->pitch == n.pitch && it->channel == n.channel)
            {
                it->offPpq = std::max (it->offPpq, n.offPpq);
                merged = true;
                ++count;
                break;
            }
        }

        if (! merged)
            unique.push_back (n);
    }

    if (dropped != nullptr)
        *dropped = count;

    return unique;
}

//==============================================================================
std::vector<Bar> buildBars (const ResolvedCapture& rc, int64_t endTick)
{
    std::vector<Bar> bars;

    for (const auto& b : rc.bars)
    {
        Bar bar;
        bar.start = toTicks (b.ppq);
        bar.num = b.num;
        bar.den = b.den;

        if (! bars.empty() && bar.start <= bars.back().start)
            continue;

        bars.push_back (bar);
    }

    if (bars.empty())
    {
        Bar bar;
        bars.push_back (bar);
    }

    // Time before the first bar line (a loop that does not start on a bar) becomes a short bar of its own.
    if (bars.front().start > 0)
    {
        Bar lead;
        lead.start = 0;
        lead.num = bars.front().num;
        lead.den = bars.front().den;
        bars.insert (bars.begin(), lead);
    }
    else
    {
        bars.front().start = 0;
    }

    for (size_t i = 0; i < bars.size(); ++i)
    {
        const auto nominal = nominalLength (bars[i].num, bars[i].den);
        bars[i].length = i + 1 < bars.size() ? bars[i + 1].start - bars[i].start : nominal;
        bars[i].irregular = bars[i].length != nominal;
    }

    // A last bar that is cut short by the end of the recording is still a whole bar on paper.
    if (bars.back().irregular && bars.size() > 1)
        bars.back().length = nominalLength (bars.back().num, bars.back().den);

    // The recording may run on past the last bar line the host reported.
    while (bars.back().start + bars.back().length < endTick)
    {
        Bar next;
        next.start = bars.back().start + bars.back().length;
        next.num = bars.back().num;
        next.den = bars.back().den;
        next.length = nominalLength (next.num, next.den);
        bars.push_back (next);
    }

    // No bars after the end.
    while (bars.size() > 1 && bars.back().start >= endTick)
        bars.pop_back();

    return bars;
}

void applyPickup (std::vector<Bar>& bars, int64_t firstOnset)
{
    if (bars.empty() || bars.front().irregular)
        return;

    auto& first = bars.front();
    const auto relative = firstOnset - first.start;

    // More than half the bar must be empty; a first note on beat 3 of 4/4 is an ordinary bar that
    // starts with a rest.
    if (relative <= 0 || relative <= first.length / 2 || relative >= first.length)
        return;

    const auto skipped = floorDiv (relative, beatTicks) * beatTicks;

    if (skipped <= 0)
        return;

    first.start += skipped;
    first.length -= skipped;
    first.pickup = true;
}

//==============================================================================
QuantizeResult quantize (const std::vector<ResolvedNote>& notes, const std::vector<Bar>& bars, const TranscriptionSettings& settings)
{
    QuantizeResult result;

    const auto straight = (int64_t) straightSlot (settings);
    const auto triplet = (int64_t) tripletSlot (settings);
    const auto halfSlot = straight / 2;

    if (notes.empty() || bars.empty())
        return result;

    struct Played
    {
        int64_t on, off;
        size_t bar;
        int64_t rel;        // onset from the bar start
        int64_t beat;       // beat index in the bar
    };

    std::vector<Played> played;
    played.reserve (notes.size());

    auto barIndexFor = [&] (int64_t tick)
    {
        // A note a little before a bar line belongs to the bar that starts there.
        const auto probe = tick + halfSlot;
        size_t index = 0;

        for (size_t i = 0; i < bars.size(); ++i)
            if (bars[i].start <= probe)
                index = i;

        return index;
    };

    for (const auto& n : notes)
    {
        Played p;
        p.on = toTicks (n.onPpq);
        p.off = std::max (p.on + 1, toTicks (n.offPpq));
        p.bar = barIndexFor (p.on);
        p.rel = std::max<int64_t> (0, p.on - bars[p.bar].start);
        p.beat = floorDiv (p.rel + halfSlot, beatTicks);
        played.push_back (p);
    }

    // Straight or triplet for every beat that holds onsets.
    struct BeatKey
    {
        size_t bar;
        int64_t beat;

        bool operator< (const BeatKey& o) const { return bar != o.bar ? bar < o.bar : beat < o.beat; }
    };

    std::map<BeatKey, std::vector<int64_t>> onsetsInBeat;

    for (const auto& p : played)
        onsetsInBeat[{ p.bar, p.beat }].push_back (p.rel - p.beat * beatTicks);

    std::map<BeatKey, bool> isTriplet;

    for (const auto& [key, xs] : onsetsInBeat)
    {
        bool useTriplet = false;

        if (triplet > 0)
        {
            int64_t errorStraight = 0, errorTriplet = 0;

            for (const auto x : xs)
            {
                errorStraight += std::llabs (x - roundToSlot (x, straight));
                errorTriplet += std::llabs (x - roundToSlot (x, triplet));
            }

            // Triplets have to be clearly better; a tie stays straight.
            useTriplet = errorTriplet + 2 < errorStraight;
        }

        isTriplet[key] = useTriplet;

        if (useTriplet)
            ++result.tripletBeats;
    }

    // Absolute beat starts that use the triplet grid, to look up where a note ends.
    std::map<int64_t, bool> tripletAt;

    for (const auto& [key, flag] : isTriplet)
        tripletAt[bars[key.bar].start + key.beat * beatTicks] = flag;

    auto quantizeEnd = [&] (int64_t absolutePosition, bool onsetBeatIsTriplet, size_t onsetBar, int64_t onsetBeat)
    {
        // The beat the end falls in, counted from the bar it is in.
        size_t barIndex = 0;

        for (size_t i = 0; i < bars.size(); ++i)
            if (bars[i].start <= absolutePosition + halfSlot)
                barIndex = i;

        const auto rel = std::max<int64_t> (0, absolutePosition - bars[barIndex].start);
        const auto beat = floorDiv (rel + halfSlot, beatTicks);
        const auto beatStart = bars[barIndex].start + beat * beatTicks;
        const auto x = absolutePosition - beatStart;

        const auto qs = beatStart + roundToSlot (x, straight);
        const auto es = std::llabs (absolutePosition - qs);

        if (triplet <= 0)
            return qs;

        const auto qt = beatStart + roundToSlot (x, triplet);
        const auto et = std::llabs (absolutePosition - qt);

        // A note that starts and ends inside one triplet beat is written in triplet values, however
        // short it was played (a 16th-length note among eighth triplets is an eighth triplet).
        if (onsetBeatIsTriplet && barIndex == onsetBar && beat == onsetBeat)
            return qt;

        const auto known = tripletAt.find (beatStart);

        if (known != tripletAt.end())
            return known->second ? (et <= es ? qt : qs) : qs;

        // An empty beat: straight, unless a triplet position is clearly closer (or the note started in a
        // triplet). A beat that a note ends in on a triplet position becomes a triplet beat.
        if (et + (onsetBeatIsTriplet ? 0 : 8) < es)
        {
            const BeatKey key { barIndex, beat };

            if (! isTriplet[key])
            {
                isTriplet[key] = true;
                ++result.tripletBeats;
            }

            tripletAt[beatStart] = true;
            return qt;
        }

        return qs;
    };

    for (const auto& p : played)
    {
        QNote q;
        const auto tripletBeat = isTriplet[{ p.bar, p.beat }];
        const auto slot = tripletBeat ? triplet : straight;
        const auto beatStart = bars[p.bar].start + p.beat * beatTicks;
        const auto x = p.on - beatStart;

        q.on = beatStart + roundToSlot (x, slot);
        q.onError = (int) (q.on - p.on);
        q.tripletBeat = tripletBeat;
        q.offGrid = std::llabs (q.onError) * 4 > slot;

        const auto end = quantizeEnd (p.off, tripletBeat, p.bar, p.beat);
        q.dur = std::max<int64_t> (end - q.on, slot);
        q.durError = (int) (q.on + q.dur - p.off);
        q.pitch = 0;
        result.notes.push_back (q);

        if (q.offGrid)
            ++result.offGrid;
    }

    // Copy the pitch data across (the loop above kept the order of `notes`).
    for (size_t i = 0; i < notes.size(); ++i)
    {
        result.notes[i].pitch = notes[i].pitch;
        result.notes[i].velocity = notes[i].velocity;
        result.notes[i].channel = notes[i].channel;
    }

    std::stable_sort (result.notes.begin(), result.notes.end(), [] (const QNote& a, const QNote& b)
    {
        return a.on != b.on ? a.on < b.on : a.pitch < b.pitch;
    });

    // The same pitch cannot sound twice at once: a note ends where the next one of its pitch begins.
    std::vector<QNote> kept;
    std::map<std::pair<int, int>, size_t> lastOfPitch;

    for (auto& q : result.notes)
    {
        const auto key = std::make_pair (q.pitch, q.channel);
        const auto it = lastOfPitch.find (key);

        if (it != lastOfPitch.end())
        {
            auto& previous = kept[it->second];

            if (previous.on == q.on)
            {
                previous.dur = std::max (previous.dur, q.dur);   // the same note twice: one note
                continue;
            }

            if (previous.on + previous.dur > q.on)
                previous.dur = q.on - previous.on;
        }

        lastOfPitch[key] = kept.size();
        kept.push_back (q);
    }

    result.notes = std::move (kept);

    for (const auto& [key, flag] : isTriplet)
        result.beats.push_back ({ bars[key.bar].start + key.beat * beatTicks, flag });

    return result;
}

//==============================================================================
std::vector<TempoMark> tempoMarks (const ResolvedCapture& rc)
{
    std::vector<TempoMark> marks;
    const auto& pts = rc.tempoMap;

    if (pts.empty())
        return marks;

    auto tickOf = [] (double ppq)
    {
        return roundToSlot (toTicks (std::max (0.0, ppq)), ticksPerQuarter / 4);
    };

    auto moving = [&] (size_t i)
    {
        return i + 1 < pts.size() && std::abs (pts[i + 1].bpm - pts[i].bpm) > 0.5;
    };

    const auto end = std::max (rc.lengthPpq, pts.back().ppq);

    marks.push_back ({ 0, (int) std::lround (pts.front().bpm), {} });
    auto shown = marks.back().bpm;

    size_t i = 0;

    while (i < pts.size())
    {
        // the next change of tempo
        size_t j = i;

        while (j < pts.size() && ! moving (j))
            ++j;

        if (j >= pts.size())
            break;

        // the change runs while consecutive points keep moving
        size_t k = j;

        while (moving (k))
            ++k;

        // points j .. k are the change; k is where the new tempo starts
        const auto steps = k - j;
        const auto span = pts[k].ppq - pts[j].ppq;
        const auto isRamp = steps >= 3 && span >= 0.5;

        size_t nextChange = k;

        while (nextChange < pts.size() && ! moving (nextChange))
            ++nextChange;

        const auto holdEnd = nextChange < pts.size() ? pts[nextChange].ppq : end;
        const auto holds = holdEnd - pts[k].ppq >= 1.0 - 1.0e-9;

        // a tempo that is already on the page is not written again (a flick away and back)
        if (holds && (int) std::lround (pts[k].bpm) != shown)
        {
            if (isRamp)
                marks.push_back ({ tickOf (pts[j].ppq), 0, pts[k].bpm > pts[j].bpm ? "accel." : "rit." });

            shown = (int) std::lround (pts[k].bpm);
            marks.push_back ({ tickOf (pts[k].ppq), shown, {} });
        }
        else if (isRamp && nextChange >= pts.size())
        {
            marks.push_back ({ tickOf (pts[j].ppq), 0, pts[k].bpm > pts[j].bpm ? "accel." : "rit." });
        }

        i = k;
    }

    return marks;
}

}  // namespace trs
