#include "CaptureEngine.h"

namespace trs
{

CaptureEngine::CaptureEngine (size_t capacity)
{
    size_t size = 16;

    while (size < capacity)
        size <<= 1;

    ring.resize (size);
    mask = size - 1;
}

void CaptureEngine::push (const Record& r) noexcept
{
    const auto write = head.load (std::memory_order_relaxed);
    const auto read = tail.load (std::memory_order_acquire);

    if (write - read >= ring.size())
    {
        dropped.fetch_add (1);
        return;
    }

    ring[write & mask] = r;
    head.store (write + 1, std::memory_order_release);
}

void CaptureEngine::pushStart (const HostBlock& b) noexcept
{
    Record r;
    r.kind = Record::Kind::start;
    r.ppq = b.ppq;
    r.bpm = b.bpm;
    r.sampleRate = b.sampleRate;
    push (r);
}

void CaptureEngine::pushBlock (const HostBlock& b) noexcept
{
    Record r;
    r.kind = Record::Kind::block;
    r.ppq = b.ppq;
    r.bpm = b.bpm;
    r.sampleRate = b.sampleRate;
    r.numSamples = b.numSamples;
    r.tsNum = b.tsNum;
    r.tsDen = b.tsDen;
    r.barStartPpq = b.barStartPpq;

    unsigned flags = 0;
    if (b.looping)     flags |= Record::looping;
    if (b.hasTimeSig)  flags |= Record::hasTimeSig;
    if (b.hasBarStart) flags |= Record::hasBarStart;
    r.flags = (uint8_t) flags;|= Record::looping;
    if (b.hasTimeSig)  r.flags |= Record::hasTimeSig;
    if (b.hasBarStart) r.flags |= Record::hasBarStart;

    push (r);
}

void CaptureEngine::pushMidi (const MidiEvent& e, double ppq, uint8_t flags) noexcept
{
    Record r;
    r.kind = Record::Kind::midi;
    r.flags = flags;
    r.ppq = ppq;
    r.midiSize = (uint8_t) e.size;

    for (int i = 0; i < e.size && i < 3; ++i)
        r.midi[i] = e.data[i];

    push (r);
}

void CaptureEngine::pushStop (double ppq, uint8_t flags) noexcept
{
    Record r;
    r.kind = Record::Kind::stop;
    r.flags = flags;
    r.ppq = ppq;
    push (r);
}

// Note-on, note-off, All Sound Off (CC 120) and All Notes Off (CC 123).
bool CaptureEngine::isRecordedMessage (const MidiEvent& e) noexcept
{
    if (e.size < 2)
        return false;

    const auto type = e.data[0] & 0xf0;

    if (type == 0x80 || type == 0x90)
        return e.size >= 3;

    return type == 0xb0 && (e.data[1] == 120 || e.data[1] == 123);
}

// Messages that end notes; the only ones kept from the block in which the host stops.
bool CaptureEngine::isReleaseMessage (const MidiEvent& e) noexcept
{
    if (! isRecordedMessage (e))
        return false;

    const auto type = e.data[0] & 0xf0;
    return type == 0x80 || type == 0xb0 || (type == 0x90 && e.data[2] == 0);
}

void CaptureEngine::process (const HostBlock& b, const MidiEvent* events, int numEvents) noexcept
{
    auto s = (State) state.load();
    const auto cmd = command.exchange (cmdNone);

    if (cmd == cmdReset)
    {
        s = State::idle;
    }
    else if (cmd == cmdArm)
    {
        if (s != State::recording)
            s = State::armed;
    }
    else if (cmd == cmdStop)
    {
        if (s == State::recording)
        {
            pushStop (b.hasPpq ? b.ppq : lastEndPpq, Record::userStop);
            s = State::stopped;
        }
        else if (s == State::armed)
        {
            s = State::idle;
        }
    }

    const auto usable = b.playing && b.hasPpq && b.hasBpm && b.bpm > 0.0 && b.sampleRate > 0.0;

    if (s == State::armed && usable)
    {
        pushStart (b);
        s = State::recording;
    }

    if (s == State::recording)
    {
        if (usable)
        {
            pushBlock (b);

            const auto quartersPerSample = b.bpm / (60.0 * b.sampleRate);

            for (int i = 0; i < numEvents; ++i)
                if (isRecordedMessage (events[i]))
                    pushMidi (events[i], b.ppq + events[i].sampleOffset * quartersPerSample, 0);

            lastEndPpq = b.ppq + b.numSamples * quartersPerSample;
        }
        else
        {
            // The host stopped (or lost its position). Live sends All Notes Off and note-offs in
            // this block; they belong to the stop position, not to a new time.
            for (int i = 0; i < numEvents; ++i)
                if (isReleaseMessage (events[i]))
                    pushMidi (events[i], lastEndPpq, Record::stopFlush);

            pushStop (lastEndPpq, 0);
            s = State::stopped;
        }
    }

    state.store ((int) s);
}

}  // namespace trs
