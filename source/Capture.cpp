#include "Capture.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <cmath>

Capture::Capture()
{
    for (auto& channel : openIndex)
        channel.fill (-1);
}

void Capture::add (const DiagnosticLog::Record& r)
{
    if (r.kind != DiagnosticLog::Record::Kind::midi || ! r.hasEventPpq || r.midiSize < 3)
        return;

    const auto msg = juce::MidiMessage (r.midi, r.midiSize);

    if (! msg.isNoteOnOrOff())
        return;

    const auto ch = juce::jlimit (1, 16, msg.getChannel()) - 1;
    const auto pitch = msg.getNoteNumber();

    const std::lock_guard<std::mutex> lock (mutex);
    auto& open = openIndex[(size_t) ch][(size_t) pitch];

    // A note-on for a note that is already held ends the held one (retrigger).
    if (open >= 0)
    {
        notes[(size_t) open].offPpq = r.eventPpq;
        open = -1;
    }

    if (msg.isNoteOn())
    {
        notes.push_back ({ r.eventPpq, -1.0, pitch, (int) msg.getVelocity(), ch + 1 });
        open = (int) notes.size() - 1;
    }
}

void Capture::clear()
{
    const std::lock_guard<std::mutex> lock (mutex);
    notes.clear();

    for (auto& channel : openIndex)
        channel.fill (-1);
}

std::vector<CapturedNote> Capture::snapshot() const
{
    const std::lock_guard<std::mutex> lock (mutex);
    return notes;
}

int Capture::size() const
{
    const std::lock_guard<std::mutex> lock (mutex);
    return (int) notes.size();
}

namespace
{
    struct FileNote
    {
        double on = 0.0, off = 0.0;
        int pitch = 0;
    };

    juce::var noteToVar (double on, double off, int pitch)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("on", on);
        o->setProperty ("off", off);
        o->setProperty ("pitch", pitch);
        return juce::var (o);
    }
}

CompareResult compareWithMidiFile (const juce::File& midiFile,
                                   const std::vector<CapturedNote>& captured,
                                   double tolerance)
{
    CompareResult result;
    auto* report = new juce::DynamicObject();
    result.report = juce::var (report);

    report->setProperty ("file", midiFile.getFullPathName());
    report->setProperty ("tolerance", tolerance);

    auto fail = [&] (const juce::String& why)
    {
        report->setProperty ("error", why);
        return result;
    };

    juce::FileInputStream in (midiFile);

    if (in.failedToOpen())
        return fail ("Could not open the MIDI file.");

    juce::MidiFile mf;

    if (! mf.readFrom (in))
        return fail ("The file is not a valid MIDI file.");

    const auto tpq = (int) mf.getTimeFormat();

    if (tpq <= 0)
        return fail ("The file uses SMPTE timing, which this test does not support.");

    report->setProperty ("ticksPerQuarter", tpq);

    std::vector<FileNote> fileNotes;

    for (int t = 0; t < mf.getNumTracks(); ++t)
    {
        juce::MidiMessageSequence seq (*mf.getTrack (t));
        seq.updateMatchedPairs();

        for (int i = 0; i < seq.getNumEvents(); ++i)
        {
            const auto& m = seq.getEventPointer (i)->message;

            if (! m.isNoteOn())
                continue;

            const auto on = m.getTimeStamp() / tpq;
            const auto offIndex = seq.getIndexOfMatchingKeyUp (i);
            const auto off = offIndex >= 0 ? seq.getEventPointer (offIndex)->message.getTimeStamp() / tpq : on;
            fileNotes.push_back ({ on, off, m.getNoteNumber() });
        }
    }

    auto byOnset = [] (const auto& a, const auto& b) { return a.on != b.on ? a.on < b.on : a.pitch < b.pitch; };
    std::sort (fileNotes.begin(), fileNotes.end(), byOnset);

    struct CapNote { double on, off; int pitch; bool matched; };
    std::vector<CapNote> cap;

    for (const auto& n : captured)
        cap.push_back ({ n.onPpq, n.offPpq >= 0.0 ? n.offPpq : n.onPpq, n.pitch, false });

    std::sort (cap.begin(), cap.end(), byOnset);

    report->setProperty ("fileNotes", (int) fileNotes.size());
    report->setProperty ("capturedNotes", (int) cap.size());

    if (fileNotes.empty())
        return fail ("The MIDI file contains no notes.");

    if (cap.empty())
        return fail ("Nothing has been captured yet. Play the clip first.");

    const auto offset = cap.front().on - fileNotes.front().on;
    report->setProperty ("offsetQuarterNotes", offset);

    int matched = 0;
    double maxOnsetError = 0.0, sumOnsetError = 0.0, maxDurationError = 0.0;
    juce::Array<juce::var> missing, extra;

    for (const auto& f : fileNotes)
    {
        int best = -1;
        double bestError = tolerance;

        for (size_t i = 0; i < cap.size(); ++i)
        {
            if (cap[i].matched || cap[i].pitch != f.pitch)
                continue;

            const auto err = std::abs ((cap[i].on - offset) - f.on);

            if (err <= bestError)
            {
                bestError = err;
                best = (int) i;
            }
        }

        if (best < 0)
        {
            if (missing.size() < 20)
                missing.add (noteToVar (f.on, f.off, f.pitch));
            continue;
        }

        auto& c = cap[(size_t) best];
        c.matched = true;
        ++matched;
        maxOnsetError = std::max (maxOnsetError, bestError);
        sumOnsetError += bestError;
        maxDurationError = std::max (maxDurationError, std::abs ((c.off - c.on) - (f.off - f.on)));
    }

    int extraCount = 0;

    for (const auto& c : cap)
    {
        if (c.matched)
            continue;

        ++extraCount;

        if (extra.size() < 20)
            extra.add (noteToVar (c.on - offset, c.off - offset, c.pitch));
    }

    const auto missingCount = (int) fileNotes.size() - matched;

    report->setProperty ("matched", matched);
    report->setProperty ("missing", missingCount);
    report->setProperty ("extra", extraCount);
    report->setProperty ("maxOnsetError", maxOnsetError);
    report->setProperty ("meanOnsetError", matched > 0 ? sumOnsetError / matched : 0.0);
    report->setProperty ("maxDurationError", maxDurationError);
    report->setProperty ("firstMissing", missing);
    report->setProperty ("firstExtra", extra);

    result.passed = missingCount == 0 && extraCount == 0;
    report->setProperty ("passed", result.passed);
    return result;
}
