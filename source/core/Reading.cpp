#include "Reading.h"

#include <algorithm>
#include <cmath>

namespace trs
{

namespace
{
    constexpr double eps = 1.0e-6;

    // A note starts at a sample, so it can land a hair before the bar line or loop start it
    // belongs to (about 2e-5 quarter notes at 127 bpm). Anything this close counts as on the line.
    constexpr double boundaryTolerance = 0.01;

    // Index of the last bar line at or before ppq (0 if there is none).
    size_t barIndexAt (const std::vector<BarStart>& bars, double ppq)
    {
        size_t found = 0;

        for (size_t i = 0; i < bars.size(); ++i)
        {
            if (bars[i].ppq > ppq + 1.0e-4)
                break;

            found = i;
        }

        return found;
    }

    //==========================================================================
    // Note-pattern search

    struct BarNote
    {
        long key = 0;         // onset, rounded to 1/100 quarter note, for ordering
        double on = 0.0;      // relative to the bar start
        double dur = 0.0;
        int pitch = 0;
        bool open = false;    // length was cut by the end of the recording
    };

    struct BarContent
    {
        double start = 0.0;
        double length = 0.0;
        std::vector<BarNote> notes;
    };

    std::vector<BarContent> collectFullBars (const RawCapture& raw)
    {
        std::vector<BarContent> out;
        const auto end = raw.endOfCapture();
        size_t next = 0;

        for (size_t i = 0; i < raw.bars.size(); ++i)
        {
            const auto s = raw.bars[i].ppq;
            const auto e = i + 1 < raw.bars.size() ? raw.bars[i + 1].ppq : s + raw.bars[i].length();

            if (s < raw.startPpq - eps || e > end + eps)
                continue;

            BarContent bar;
            bar.start = s;
            bar.length = e - s;

            while (next < raw.notes.size() && raw.notes[next].onPpq < s - boundaryTolerance)
                ++next;

            for (auto j = next; j < raw.notes.size() && raw.notes[j].onPpq < e - boundaryTolerance; ++j)
            {
                const auto& n = raw.notes[j];
                BarNote bn;
                bn.on = n.onPpq - s;
                bn.key = std::lround (bn.on * 100.0);
                bn.dur = (n.offPpq >= 0.0 ? n.offPpq : end) - n.onPpq;
                bn.pitch = n.pitch;
                bn.open = n.heldAtStop || n.offPpq < 0.0;
                bar.notes.push_back (bn);
            }

            std::sort (bar.notes.begin(), bar.notes.end(), [] (const BarNote& a, const BarNote& b)
            {
                return a.key != b.key ? a.key < b.key : a.pitch < b.pitch;
            });

            out.push_back (std::move (bar));
        }

        return out;
    }

    bool barsMatch (const BarContent& a, const BarContent& b)
    {
        if (std::abs (a.length - b.length) > 1.0e-4 || a.notes.size() != b.notes.size())
            return false;

        for (size_t k = 0; k < a.notes.size(); ++k)
        {
            const auto& x = a.notes[k];
            const auto& y = b.notes[k];

            if (x.pitch != y.pitch || std::abs (x.on - y.on) > 0.02)
                return false;

            if (! x.open && ! y.open && std::abs (x.dur - y.dur) > 0.1)
                return false;
        }

        return true;
    }

    // A repeat is accepted when at most a tenth of the bar pairs differ and the differences
    // never fill a whole period (that would be a different section, not a variation).
    constexpr double acceptedMismatch = 0.1;

    Detection detectFromNotes (const RawCapture& raw)
    {
        Detection d;
        const auto bars = collectFullBars (raw);
        const auto n = (int) bars.size();

        double bestShare = 2.0, chosenShare = 2.0;
        int bestPeriod = 0;
        int chosenPeriod = 0;

        for (int period = 1; period * 2 <= n; ++period)
        {
            int mismatches = 0, run = 0, longestRun = 0;
            const auto pairs = n - period;

            for (int i = 0; i < pairs; ++i)
            {
                if (barsMatch (bars[(size_t) i], bars[(size_t) (i + period)]))
                {
                    run = 0;
                }
                else
                {
                    ++mismatches;
                    longestRun = std::max (longestRun, ++run);
                }
            }

            size_t notesInPeriod = 0;

            for (int i = 0; i < period; ++i)
                notesInPeriod += bars[(size_t) i].notes.size();

            if (notesInPeriod == 0)
                continue;

            const auto share = (double) mismatches / pairs;

            if (share < bestShare)
            {
                bestShare = share;
                bestPeriod = period;
            }

            if (chosenPeriod == 0 && share <= acceptedMismatch && longestRun < period)
            {
                chosenPeriod = period;
                chosenShare = share;
            }
        }

        const auto candidate = chosenPeriod != 0 ? chosenPeriod : (bestShare <= 0.5 ? bestPeriod : 0);

        if (candidate != 0)
        {
            d.reading.loopStartPpq = bars.front().start;
            d.reading.loopLengthPpq = bars[(size_t) candidate].start - bars.front().start;
            d.candidateBars = candidate;
            d.candidateMismatch = candidate == chosenPeriod ? chosenShare : bestShare;

            if (chosenPeriod != 0)
            {
                d.reading.mode = ReadingMode::oneLoop;
                d.reading.source = ReadingSource::detected;
                return d;
            }
        }

        d.reading.mode = ReadingMode::asPlayed;
        d.reading.source = ReadingSource::detected;
        return d;
    }

    //==========================================================================
    // Resolving

    // The tempo in force at ppq, followed by the tempo changes up to (not including) endPpq,
    // all relative to originPpq.
    std::vector<TempoPoint> tempoWindow (const std::vector<TempoPoint>& map, double fromPpq, double toPpq)
    {
        std::vector<TempoPoint> out;
        const TempoPoint* current = nullptr;

        for (const auto& t : map)
        {
            if (t.ppq <= fromPpq + eps)
            {
                current = &t;
                continue;
            }

            if (t.ppq >= toPpq - eps)
                break;

            if (out.empty() && current != nullptr)
                out.push_back ({ 0.0, current->bpm });

            out.push_back ({ t.ppq - fromPpq, t.bpm });
        }

        if (out.empty())
        {
            if (current != nullptr)
                out.push_back ({ 0.0, current->bpm });
            else if (! map.empty())
                out.push_back ({ 0.0, map.front().bpm });
        }

        return out;
    }

    std::vector<BarStart> barWindow (const std::vector<BarStart>& bars, double fromPpq, double toPpq)
    {
        std::vector<BarStart> out;

        for (const auto& b : bars)
            if (b.ppq >= fromPpq - eps && b.ppq < toPpq - eps)
                out.push_back ({ b.ppq - fromPpq, b.num, b.den });

        return out;
    }

    ResolvedCapture resolveAsPlayed (const RawCapture& raw, const Reading& reading)
    {
        ResolvedCapture out;
        out.reading = reading;
        out.reading.mode = ReadingMode::asPlayed;

        const auto end = raw.endOfCapture();

        // Start at the bar line before the first block, so a pickup bar keeps its place.
        out.originPpq = raw.bars.empty() || raw.bars.front().ppq > raw.startPpq + eps
                            ? raw.startPpq
                            : raw.bars[barIndexAt (raw.bars, raw.startPpq)].ppq;
        out.lengthPpq = std::max (0.0, end - out.originPpq);
        out.numPasses = 1;

        for (const auto& n : raw.notes)
        {
            ResolvedNote r;
            r.onPpq = n.onPpq - out.originPpq;
            r.offPpq = (n.offPpq >= 0.0 ? n.offPpq : end) - out.originPpq;
            r.pitch = n.pitch;
            r.velocity = n.velocity;
            r.channel = n.channel;
            out.notes.push_back (r);
        }

        out.bars = barWindow (raw.bars, out.originPpq, end + 1.0);
        out.tempoMap = tempoWindow (raw.tempoMap, out.originPpq, end + 1.0);
        return out;
    }

    ResolvedCapture resolveOneLoop (const RawCapture& raw, const Reading& reading)
    {
        ResolvedCapture out;
        out.reading = reading;

        const auto phi = reading.loopStartPpq;
        const auto len = reading.loopLengthPpq;
        const auto end = raw.endOfCapture();

        out.originPpq = phi;
        out.lengthPpq = len;

        // The pass that contains the stop, and how far into it the stop is.
        const auto q = (long) std::floor ((end - phi + boundaryTolerance) / len);
        const auto stopPhase = end - phi - (double) q * len;

        auto passOf = [&] (double ppq) { return (long) std::floor ((ppq - phi + boundaryTolerance) / len); };
        auto phaseOf = [&] (double ppq, long pass) { return std::max (0.0, ppq - phi - (double) pass * len); };

        // The pass each stretch of the loop is taken from: the final pass up to the stop point,
        // the pass before it for the rest.
        auto ownerPass = [&] (double phase) { return phase < stopPhase - boundaryTolerance ? q : q - 1; };

        const auto lastPass = stopPhase > boundaryTolerance ? q : q - 1;
        out.numPasses = (int) std::max<long> (1, lastPass - passOf (raw.startPpq) + 1);

        for (const auto& n : raw.notes)
        {
            const auto pass = passOf (n.onPpq);
            const auto phase = phaseOf (n.onPpq, pass);

            if (pass != ownerPass (phase))
                continue;

            auto duration = (n.offPpq >= 0.0 ? n.offPpq : end) - n.onPpq;

            // A note still sounding at the stop takes the length of the same note one pass earlier.
            if (n.heldAtStop)
            {
                for (const auto& m : raw.notes)
                {
                    if (m.pitch != n.pitch || m.channel != n.channel || m.heldAtStop || m.offPpq < 0.0)
                        continue;

                    if (passOf (m.onPpq) != pass - 1 || std::abs (phaseOf (m.onPpq, pass - 1) - phase) > 0.02)
                        continue;

                    duration = m.offPpq - m.onPpq;
                    break;
                }
            }

            ResolvedNote r;
            r.onPpq = phase;
            r.offPpq = phase + duration;
            r.pitch = n.pitch;
            r.velocity = n.velocity;
            r.channel = n.channel;
            r.pass = (int) pass;
            out.notes.push_back (r);
        }

        std::stable_sort (out.notes.begin(), out.notes.end(), [] (const ResolvedNote& a, const ResolvedNote& b)
        {
            return a.onPpq < b.onPpq;
        });

        // Bar lines and tempo come from the latest pass that was recorded in full.
        long refPass = q;
        out.barsComplete = false;

        for (long p = q - 1; p >= passOf (raw.startPpq); --p)
        {
            if (phi + (double) p * len >= raw.startPpq - eps)
            {
                refPass = p;
                out.barsComplete = true;
                break;
            }
        }

        const auto from = phi + (double) refPass * len;
        out.bars = barWindow (raw.bars, from, from + len);
        out.tempoMap = tempoWindow (raw.tempoMap, from, from + len);
        return out;
    }
}

//==============================================================================
double barsToPpq (const std::vector<BarStart>& bars, double startPpq, int numBars)
{
    if (numBars <= 0)
        return 0.0;

    if (bars.empty())
        return 4.0 * numBars;

    const auto first = barIndexAt (bars, startPpq);
    auto end = bars[first].ppq;

    for (int k = 0; k < numBars; ++k)
    {
        const auto idx = first + (size_t) k;
        end = idx + 1 < bars.size() ? bars[idx + 1].ppq : end + bars.back().length();
    }

    return end - startPpq;
}

int ppqToBars (const std::vector<BarStart>& bars, double startPpq, double lengthPpq)
{
    if (lengthPpq <= 0.0)
        return 0;

    if (bars.empty())
        return std::max (1, (int) std::lround (lengthPpq / 4.0));

    const auto first = barIndexAt (bars, startPpq);
    const auto target = startPpq + lengthPpq;
    auto pos = bars[first].ppq;
    int count = 0;

    for (;;)
    {
        const auto idx = first + (size_t) count;
        const auto next = idx + 1 < bars.size() ? bars[idx + 1].ppq : pos + bars.back().length();

        if (next > target + 1.0e-3)
            break;

        pos = next;
        ++count;
    }

    return std::max (1, count);
}

Detection detectReading (const RawCapture& raw)
{
    // 1. The host wrapped an Arrangement loop: it told us the loop length.
    for (size_t k = 1; k < raw.segments.size(); ++k)
    {
        const auto& s = raw.segments[k];

        if (! s.wrap)
            continue;

        const auto length = raw.segments[k - 1].songEndPpq - s.songStartPpq;

        if (length > 0.01)
        {
            Detection d;
            d.reading.mode = ReadingMode::oneLoop;
            d.reading.loopStartPpq = s.capStartPpq;
            d.reading.loopLengthPpq = length;
            d.reading.source = ReadingSource::host;
            d.candidateMismatch = 0.0;
            d.candidateBars = ppqToBars (raw.bars, s.capStartPpq, length);
            return d;
        }
    }

    // 2. Session View gives no signal, so look for a bar pattern that repeats.
    return detectFromNotes (raw);
}

ResolvedCapture resolve (const RawCapture& raw, const Reading& reading)
{
    if (reading.mode == ReadingMode::oneLoop && reading.loopLengthPpq > 0.0)
        return resolveOneLoop (raw, reading);

    return resolveAsPlayed (raw, reading);
}

}  // namespace trs
