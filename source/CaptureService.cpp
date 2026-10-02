#include "CaptureService.h"

namespace
{
    const char* stateName (trs::CaptureEngine::State s)
    {
        switch (s)
        {
            case trs::CaptureEngine::State::idle:      return "idle";
            case trs::CaptureEngine::State::armed:     return "armed";
            case trs::CaptureEngine::State::recording: return "recording";
            case trs::CaptureEngine::State::stopped:   return "stopped";
        }

        return "idle";
    }

    const char* modeName (trs::ReadingMode m)
    {
        return m == trs::ReadingMode::oneLoop ? "oneLoop" : "asPlayed";
    }

    const char* sourceName (trs::ReadingSource s)
    {
        switch (s)
        {
            case trs::ReadingSource::host:     return "host";
            case trs::ReadingSource::detected: return "detected";
            case trs::ReadingSource::user:     return "user";
            case trs::ReadingSource::none:     break;
        }

        return "none";
    }

    // The preview is only a picture; very long captures are cut off.
    constexpr size_t maxPreviewNotes = 20000;
}

CaptureService::CaptureService()
    : juce::Thread ("Transcriber capture")
{
    startThread();
}

CaptureService::~CaptureService()
{
    stopThread (2000);
}

void CaptureService::run()
{
    while (! threadShouldExit())
    {
        wait (10);
        drainNow();
    }
}

void CaptureService::drainNow()
{
    const std::lock_guard<std::mutex> lock (mutex);
    engine.drain ([this] (const trs::Record& r) { model.consume (r); });

    const auto dropped = engine.getDroppedCount();

    if (dropped != lastDropped)
    {
        lastDropped = dropped;
        model.markIncomplete();
    }
}

void CaptureService::clear()
{
    engine.requestReset();

    const std::lock_guard<std::mutex> lock (mutex);
    model.clear();
}

void CaptureService::setMode (trs::ReadingMode mode)
{
    const std::lock_guard<std::mutex> lock (mutex);
    model.setMode (mode);
}

void CaptureService::setLoopBars (int numBars)
{
    const std::lock_guard<std::mutex> lock (mutex);
    model.setLoopBars (numBars);
}

void CaptureService::redetect()
{
    const std::lock_guard<std::mutex> lock (mutex);

    if (model.raw().stopped)
        model.redetect();
}

uint64_t CaptureService::getRevision() const
{
    const std::lock_guard<std::mutex> lock (mutex);
    return model.revision();
}

juce::var CaptureService::getStatus() const
{
    auto state = engine.getState();
    const auto pending = engine.getPending();
    const std::lock_guard<std::mutex> lock (mutex);

    // Show a command that the audio thread has not seen yet as if it had taken effect.
    using Pending = trs::CaptureEngine::Pending;
    using State = trs::CaptureEngine::State;

    if (pending == Pending::arm && state != State::recording)
        state = State::armed;
    else if (pending == Pending::reset || (pending == Pending::stop && state == State::armed))
        state = State::idle;

    const auto& raw = model.raw();
    const auto& reading = model.reading();
    const auto& detection = model.detection();
    const auto& resolved = model.resolved();

    auto* o = new juce::DynamicObject();
    o->setProperty ("state", stateName (state));
    o->setProperty ("revision", (juce::int64) model.revision());
    o->setProperty ("rawNotes", (int) raw.notes.size());
    o->setProperty ("scoreNotes", (int) resolved.notes.size());
    o->setProperty ("segments", (int) raw.segments.size());
    o->setProperty ("bars", (int) raw.bars.size());
    o->setProperty ("passes", resolved.numPasses);
    o->setProperty ("startPpq", raw.startPpq);
    o->setProperty ("endPpq", raw.endOfCapture());
    o->setProperty ("stoppedByUser", raw.stoppedByUser);
    o->setProperty ("incomplete", raw.incomplete);
    o->setProperty ("dropped", (juce::int64) engine.getDroppedCount());
    o->setProperty ("mode", modeName (reading.mode));
    o->setProperty ("source", sourceName (reading.source));
    o->setProperty ("loopStartPpq", reading.loopStartPpq);
    o->setProperty ("loopLengthPpq", reading.loopLengthPpq);
    o->setProperty ("loopBars", reading.loopLengthPpq > 0.0 ? model.getLoopBars() : 0);
    o->setProperty ("candidateBars", detection.candidateBars);
    o->setProperty ("candidateMismatch", detection.candidateMismatch);
    return juce::var (o);
}

juce::var CaptureService::getPreview() const
{
    const std::lock_guard<std::mutex> lock (mutex);
    const auto& resolved = model.resolved();

    auto* o = new juce::DynamicObject();
    o->setProperty ("revision", (juce::int64) model.revision());
    o->setProperty ("length", resolved.lengthPpq);

    juce::Array<juce::var> notes;
    const auto count = std::min (resolved.notes.size(), maxPreviewNotes);
    notes.ensureStorageAllocated ((int) count * 5);

    // Five numbers per note: on, off, pitch, velocity, pass.
    for (size_t i = 0; i < count; ++i)
    {
        const auto& n = resolved.notes[i];
        notes.add (n.onPpq);
        notes.add (n.offPpq);
        notes.add (n.pitch);
        notes.add (n.velocity);
        notes.add (n.pass);
    }

    juce::Array<juce::var> bars;

    for (const auto& b : resolved.bars)
    {
        bars.add (b.ppq);
        bars.add (b.num);
        bars.add (b.den);
    }

    o->setProperty ("notes", notes);
    o->setProperty ("bars", bars);
    o->setProperty ("truncated", resolved.notes.size() > count);
    return juce::var (o);
}

std::vector<CapturedNote> CaptureService::getResolvedNotes() const
{
    const std::lock_guard<std::mutex> lock (mutex);
    std::vector<CapturedNote> out;

    for (const auto& n : model.resolved().notes)
        out.push_back ({ n.onPpq, n.offPpq, n.pitch, n.velocity, n.channel });

    return out;
}
