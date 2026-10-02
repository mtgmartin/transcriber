#include "CaptureModel.h"

#include <algorithm>
#include <cmath>

namespace trs
{

CaptureModel::CaptureModel()
{
    clear();
}

void CaptureModel::clear()
{
    capture = {};
    currentReading = {};
    lastDetection = {};
    haveBlock = false;
    songEnd = 0.0;
    offset = 0.0;

    for (auto& channel : openNote)
        channel.fill (-1);

    ++revisionCounter;
}

void CaptureModel::markIncomplete()
{
    if (! capture.incomplete)
    {
        capture.incomplete = true;
        ++revisionCounter;
    }
}

void CaptureModel::consume (const Record& r)
{
    switch (r.kind)
    {
        case Record::Kind::start:  onStart (r); break;
        case Record::Kind::block:  if (capture.recording) onBlock (r); break;
        case Record::Kind::midi:   if (capture.recording) onMidi (r);  break;
        case Record::Kind::stop:   if (capture.recording) onStop (r);  break;
    }
}

void CaptureModel::onStart (const Record& r)
{
    clear();
    capture.recording = true;
    capture.sampleRate = r.sampleRate;
    capture.startPpq = capture.endPpq = r.ppq;
    ++revisionCounter;
}

void CaptureModel::onBlock (const Record& r)
{
    if (r.sampleRate <= 0.0 || r.bpm <= 0.0)
        return;

    const auto duration = r.numSamples / r.sampleRate * r.bpm / 60.0;

    if (! haveBlock)
    {
        capture.startPpq = r.ppq;
        capture.segments.push_back ({ r.ppq, r.ppq + duration, r.ppq, r.ppq + duration, false });
        offset = 0.0;
        haveBlock = true;
    }
    else
    {
        // The host reports the tempo one block late, so the expected position can be off by a
        // little when the tempo changes. Anything larger is a jump.
        const auto tolerance = 0.05 + 2.0 * duration;

        if (std::abs (r.ppq - songEnd) > tolerance)
        {
            const auto capStart = capture.segments.back().capEndPpq;
            Segment s;
            s.songStartPpq = r.ppq;
            s.songEndPpq = r.ppq + duration;
            s.capStartPpq = capStart;
            s.capEndPpq = capStart + duration;
            s.wrap = r.ppq < songEnd && (r.flags & Record::looping) != 0;
            capture.segments.push_back (s);
            offset = capStart - r.ppq;
        }
        else
        {
            auto& s = capture.segments.back();
            s.songEndPpq = r.ppq + duration;
            s.capEndPpq = r.ppq + duration + offset;
        }
    }

    songEnd = r.ppq + duration;
    capture.endPpq = songEnd + offset;

    const auto capPpq = r.ppq + offset;

    if (capture.tempoMap.empty() || std::abs (r.bpm - capture.tempoMap.back().bpm) > 1.0e-3)
        capture.tempoMap.push_back ({ capPpq, r.bpm });

    if (r.flags & Record::hasBarStart)
    {
        const auto barPpq = r.barStartPpq + offset;
        const auto num = (r.flags & Record::hasTimeSig) ? r.tsNum : (capture.bars.empty() ? 4 : capture.bars.back().num);
        const auto den = (r.flags & Record::hasTimeSig) ? r.tsDen : (capture.bars.empty() ? 4 : capture.bars.back().den);

        if (capture.bars.empty() || barPpq > capture.bars.back().ppq + 1.0e-6)
            capture.bars.push_back ({ barPpq, num, den });
        else if (std::abs (barPpq - capture.bars.back().ppq) <= 1.0e-6)
        {
            capture.bars.back().num = num;
            capture.bars.back().den = den;
        }
        // A bar line before the latest one (a loop that wraps mid-bar) is ignored.
    }

    ++revisionCounter;
}

void CaptureModel::closeNote (int channelIndex, int pitch, double ppq, bool heldAtStop)
{
    auto& index = openNote[(size_t) channelIndex][(size_t) pitch];

    if (index < 0)
        return;

    auto& n = capture.notes[(size_t) index];
    n.offPpq = std::max (ppq, n.onPpq);
    n.heldAtStop = heldAtStop;
    index = -1;
}

void CaptureModel::closeAll (int channelIndex, double ppq, bool heldAtStop)
{
    for (int pitch = 0; pitch < 128; ++pitch)
        closeNote (channelIndex, pitch, ppq, heldAtStop);
}

void CaptureModel::onMidi (const Record& r)
{
    if (! haveBlock || r.midiSize < 2)
        return;

    const auto status = r.midi[0] & 0xf0;
    const auto channelIndex = r.midi[0] & 0x0f;
    const auto ppq = r.ppq + offset;
    const bool flush = (r.flags & Record::stopFlush) != 0;

    if (status == 0xb0)
    {
        if (r.midi[1] == 120 || r.midi[1] == 123)
            closeAll (channelIndex, ppq, flush);
    }
    else if (status == 0x80 || status == 0x90)
    {
        if (r.midiSize < 3)
            return;

        const auto pitch = r.midi[1] & 0x7f;
        const auto velocity = r.midi[2] & 0x7f;

        // A note-on for a note that is still held ends the held one (retrigger).
        closeNote (channelIndex, pitch, ppq, flush);

        if (status == 0x90 && velocity > 0 && ! flush)
        {
            RawNote n;
            n.onPpq = ppq;
            n.pitch = pitch;
            n.velocity = velocity;
            n.channel = channelIndex + 1;
            capture.notes.push_back (n);
            openNote[(size_t) channelIndex][(size_t) pitch] = (int) capture.notes.size() - 1;
        }
    }
    else
    {
        return;
    }

    ++revisionCounter;
}

void CaptureModel::onStop (const Record& r)
{
    const auto ppq = (haveBlock ? r.ppq + offset : r.ppq);

    // Notes the host cut off by stopping were sounding when the recording ended.
    for (int channel = 0; channel < 16; ++channel)
        closeAll (channel, ppq, true);

    capture.recording = false;
    capture.stopped = true;
    capture.stoppedByUser = (r.flags & Record::userStop) != 0;
    capture.stopPpq = std::max (ppq, capture.startPpq);
    capture.endPpq = capture.stopPpq;
    ++stopCounter;

    redetect();
}

void CaptureModel::redetect()
{
    lastDetection = detectReading (capture);
    currentReading = lastDetection.reading;
    ++revisionCounter;
}

void CaptureModel::setMode (ReadingMode mode)
{
    currentReading = readingWithMode (capture, currentReading, mode);
    ++revisionCounter;
}

void CaptureModel::setLoopBars (int numBars)
{
    currentReading = readingWithLoopBars (capture, currentReading, numBars);
    ++revisionCounter;
}

void CaptureModel::setLoop (double startPpq, double lengthPpq)
{
    currentReading = readingWithLoop (currentReading, startPpq, lengthPpq);
    ++revisionCounter;
}

int CaptureModel::getLoopBars() const
{
    return loopBarsOf (capture, currentReading);
}

const ResolvedCapture& CaptureModel::resolved() const
{
    if (cacheRevision != revisionCounter)
    {
        cache = resolve (capture, currentReading);
        cacheRevision = revisionCounter;
    }

    return cache;
}

}  // namespace trs
