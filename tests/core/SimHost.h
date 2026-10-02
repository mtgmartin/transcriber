#pragma once

#include "core/CaptureEngine.h"
#include "core/CaptureModel.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <set>
#include <utility>
#include <vector>

namespace sim
{

// A note as a clip would play it, in song time.
struct Note
{
    double on = 0.0, off = 0.0;
    int pitch = 60;
    int velocity = 100;
    int channel = 0;
};

struct Msg
{
    double ppq = 0.0;
    bool on = false;
    int pitch = 0, velocity = 0, channel = 0;
};

// Turns notes into the messages a clip would send (offs before ons at the same position).
inline std::vector<Msg> toMessages (const std::vector<Note>& notes)
{
    std::vector<Msg> out;

    for (const auto& n : notes)
    {
        out.push_back ({ n.on, true, n.pitch, n.velocity, n.channel });
        out.push_back ({ n.off, false, n.pitch, 0, n.channel });
    }

    std::stable_sort (out.begin(), out.end(), [] (const Msg& a, const Msg& b)
    {
        return a.ppq != b.ppq ? a.ppq < b.ppq : (! a.on && b.on);
    });

    return out;
}

// A Session clip launched at startPpq that loops until endPpq. A note that would run past
// the clip's end is cut there, as Live does.
inline std::vector<Note> loopClip (const std::vector<Note>& clip, double clipLength, double startPpq, double endPpq)
{
    std::vector<Note> out;

    for (double base = startPpq; base < endPpq - 1.0e-9; base += clipLength)
    {
        for (auto n : clip)
        {
            const auto on = base + n.on;
            const auto off = std::min (base + n.off, base + clipLength);

            if (on >= endPpq - 1.0e-9)
                continue;

            n.on = on;
            n.off = off;
            out.push_back (n);
        }
    }

    return out;
}

// Plays messages into a CaptureEngine the way Live does: fixed-size blocks, positions in
// quarter notes, bar starts from the meter map, and the block split at an Arrangement loop.
class Host
{
public:
    // drainIntoModel = false leaves the engine's records to someone else (a CaptureService thread).
    Host (trs::CaptureEngine& e, trs::CaptureModel& m, double rate = 48000.0, int block = 256, bool drainIntoModel = true)
        : engine (e), model (m), sampleRate (rate), blockSize (block), drainRecords (drainIntoModel)
    {
        meters.push_back ({ 0.0, 4, 4 });
    }

    // Messages for the pass with this index (the same list for every pass by default).
    void setMessages (std::vector<Msg> m)
    {
        lists.assign (1, std::move (m));
    }

    void setMessagesPerPass (std::vector<std::vector<Msg>> perPass)
    {
        lists = std::move (perPass);
    }

    void setMeters (std::vector<trs::BarStart> m) { meters = std::move (m); }

    // Arrangement loop. Live reports the looping flag while the loop is on.
    void setLoop (double start, double end) { loopStart = start; loopEnd = end; loopOn = true; }

    void setBpm (double b) { bpm = b; }

    // The bpm Live reports lags the one it plays by this many blocks (as seen in Phase 1).
    std::function<double (double ppq)> tempoCurve;   // true tempo as a function of position
    int reportLag = 0;

    void play (double startPpq)
    {
        ppq = startPpq;
        playing = true;
        pass = 0;
        held.clear();
    }

    void run (double beats)
    {
        const auto target = totalBeats + beats;

        while (totalBeats < target - 1.0e-9)
            block (blockSize);
    }

    // Stops the transport: one block that is not playing, with All Notes Off and note-offs.
    void stop (bool withNoteOffs = true)
    {
        std::vector<trs::MidiEvent> events;

        if (withNoteOffs)
        {
            for (const auto& h : held)
            {
                trs::MidiEvent e;
                e.size = 3;
                e.data[0] = (uint8_t) (0x80 | h.second);
                e.data[1] = (uint8_t) h.first;
                e.data[2] = 0;
                events.push_back (e);
            }

            trs::MidiEvent cc;
            cc.size = 3;
            cc.data[0] = 0xb0;
            cc.data[1] = 123;
            cc.data[2] = 0;
            events.push_back (cc);
        }

        playing = false;
        auto hb = makeBlock (blockSize);
        engine.process (hb, events.data(), (int) events.size());
        held.clear();
        drain();
    }

    void drain()
    {
        if (! drainRecords)
            return;

        engine.drain ([this] (const trs::Record& r) { model.consume (r); });
    }

    double position() const { return ppq; }
    int currentPass() const { return pass; }

private:
    trs::HostBlock makeBlock (int n) const
    {
        trs::HostBlock hb;
        hb.playing = playing;
        hb.looping = playing && loopOn;
        hb.hasPpq = hb.hasBpm = hb.hasTimeSig = hb.hasBarStart = true;
        hb.ppq = ppq;
        hb.bpm = bpm;
        hb.numSamples = n;
        hb.sampleRate = sampleRate;

        const trs::BarStart* region = &meters.front();

        for (const auto& m : meters)
            if (m.ppq <= ppq + 1.0e-9)
                region = &m;

        const auto len = region->length();
        hb.barStartPpq = region->ppq + std::floor ((ppq - region->ppq) / len + 1.0e-9) * len;
        hb.tsNum = region->num;
        hb.tsDen = region->den;
        return hb;
    }

    void block (int n)
    {
        if (tempoCurve)
        {
            bpm = tempoCurve (ppq);
        }

        auto samples = n;

        // Live ends the block exactly at the loop end and starts the next one at the loop start.
        if (loopOn && ppq < loopEnd - 1.0e-9)
        {
            const auto toEnd = (loopEnd - ppq) * 60.0 / bpm * sampleRate;

            if (toEnd < n - 1.0e-9)
                samples = std::max (1, (int) std::lround (toEnd));
        }

        auto hb = makeBlock (samples);

        if (reportLag > 0)
        {
            reportedBpm.push_back (bpm);

            if ((int) reportedBpm.size() > reportLag + 1)
                reportedBpm.erase (reportedBpm.begin());

            hb.bpm = reportedBpm.front();
        }

        const auto span = samples * bpm / (60.0 * sampleRate);
        auto p1 = ppq + span;

        if (loopOn && samples < n)
            p1 = loopEnd;   // the whole stretch up to the loop end belongs to this block

        std::vector<trs::MidiEvent> events;
        const auto& list = lists[(size_t) std::min<int> (pass, (int) lists.size() - 1)];

        auto it = std::lower_bound (list.begin(), list.end(), ppq - 1.0e-9, [] (const Msg& m, double v) { return m.ppq < v; });

        for (; it != list.end() && it->ppq < p1 - 1.0e-9; ++it)
        {
            trs::MidiEvent e;
            e.sampleOffset = std::min (samples - 1, (int) std::lround ((it->ppq - ppq) * 60.0 / bpm * sampleRate));
            e.size = 3;
            e.data[0] = (uint8_t) ((it->on ? 0x90 : 0x80) | it->channel);
            e.data[1] = (uint8_t) it->pitch;
            e.data[2] = (uint8_t) it->velocity;
            events.push_back (e);

            if (it->on)
                held.insert ({ it->pitch, it->channel });
            else
                held.erase ({ it->pitch, it->channel });
        }

        engine.process (hb, events.data(), (int) events.size());
        drain();

        totalBeats += span;

        ppq = (loopOn && samples < n) ? loopEnd : ppq + span;

        if (loopOn && ppq >= loopEnd - 1.0e-6)
        {
            ppq = loopStart;
            ++pass;
        }
    }

    trs::CaptureEngine& engine;
    trs::CaptureModel& model;
    double sampleRate;
    int blockSize;
    bool drainRecords;

    double ppq = 0.0, bpm = 120.0, totalBeats = 0.0;
    bool playing = false, loopOn = false;
    double loopStart = 0.0, loopEnd = 0.0;
    int pass = 0;
    std::vector<std::vector<Msg>> lists { {} };
    std::vector<trs::BarStart> meters;
    std::set<std::pair<int, int>> held;   // pitch, channel
    std::vector<double> reportedBpm;
};

}  // namespace sim
