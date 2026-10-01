#include "DiagnosticLog.h"

#include <juce_audio_basics/juce_audio_basics.h>

bool DiagnosticLog::BlockInfo::sameTransportAs (const BlockInfo& o) const noexcept
{
    // Position fields are excluded: they move while stopped in some hosts.
    return flags == o.flags && bpm == o.bpm && tsNum == o.tsNum && tsDen == o.tsDen
        && loopStart == o.loopStart && loopEnd == o.loopEnd;
}

static juce::File makeLogFile (const juce::String& instanceId)
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Transcriber").getChildFile ("logs");

    const auto stamp = juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
    return dir.getChildFile ("phase1-" + stamp + "-" + instanceId + ".jsonl");
}

DiagnosticLog::DiagnosticLog (const juce::String& instanceId)
    : juce::Thread ("Transcriber log writer"),
      file (makeLogFile (instanceId)),
      buffer (capacity)
{
    startThread();
}

void DiagnosticLog::openStream()
{
    if (stream != nullptr || streamFailed)
        return;

    file.getParentDirectory().createDirectory();
    stream = std::make_unique<juce::FileOutputStream> (file);

    if (stream->failedToOpen())
    {
        stream.reset();
        streamFailed = true;
    }
}

DiagnosticLog::~DiagnosticLog()
{
    stopThread (2000);
    drain();

    if (stream != nullptr)
        stream->flush();
}

void DiagnosticLog::push (const Record& r) noexcept
{
    if (! enabled.load (std::memory_order_relaxed))
        return;

    auto scope = fifo.write (1);

    if (scope.blockSize1 > 0)
        buffer[(size_t) scope.startIndex1] = r;
    else if (scope.blockSize2 > 0)
        buffer[(size_t) scope.startIndex2] = r;
    else
        dropped.fetch_add (1);
}

void DiagnosticLog::logEvent (const juce::String& type, const juce::var& data)
{
    if (! enabled.load())
        return;

    auto* obj = new juce::DynamicObject();
    obj->setProperty ("t", "event");
    obj->setProperty ("wallMs", juce::Time::getMillisecondCounterHiRes());
    obj->setProperty ("type", type);

    if (! data.isVoid())
        obj->setProperty ("data", data);

    const std::lock_guard<std::mutex> lock (eventMutex);
    pendingEvents.emplace_back (obj);
}

void DiagnosticLog::run()
{
    while (! threadShouldExit())
    {
        wait (50.0);
        drain();
    }
}

void DiagnosticLog::drain()
{
    std::vector<juce::var> events;
    {
        const std::lock_guard<std::mutex> lock (eventMutex);
        events.swap (pendingEvents);
    }

    for (const auto& e : events)
        writeLine (e);

    const auto ready = fifo.getNumReady();

    if (ready > 0)
    {
        auto scope = fifo.read (ready);

        auto handle = [this] (int start, int size)
        {
            for (int i = start; i < start + size; ++i)
            {
                const auto& r = buffer[(size_t) i];

                writeLine (toVar (r));
            }
        };

        handle (scope.startIndex1, scope.blockSize1);
        handle (scope.startIndex2, scope.blockSize2);
    }

    if (stream != nullptr)
        stream->flush();
}

void DiagnosticLog::writeLine (const juce::var& object)
{
    openStream();

    if (stream == nullptr)
        return;

    stream->writeText (juce::JSON::toString (object, true) + "\n", false, false, nullptr);
    linesWritten.fetch_add (1);
}

juce::var DiagnosticLog::toVar (const Record& r)
{
    auto* obj = new juce::DynamicObject();
    const auto& b = r.info;
    const auto f = b.flags;

    obj->setProperty ("t", r.kind == Record::Kind::block ? "block" : "midi");
    obj->setProperty ("block", (juce::int64) r.blockIndex);
    obj->setProperty ("wallMs", r.wallMs);

    if (r.kind == Record::Kind::block)
    {
        obj->setProperty ("n", r.numSamples);
        obj->setProperty ("hasPos", (f & hasPosition) != 0);
        obj->setProperty ("playing", (f & playing) != 0);
        obj->setProperty ("looping", (f & looping) != 0);
        obj->setProperty ("recording", (f & recording) != 0);
        obj->setProperty ("offline", (f & nonRealtime) != 0);

        if (f & hasPpq)         obj->setProperty ("ppq", b.ppq);
        if (f & hasBpm)         obj->setProperty ("bpm", b.bpm);
        if (f & hasTimeSig)     obj->setProperty ("ts", juce::String (b.tsNum) + "/" + juce::String (b.tsDen));
        if (f & hasLastBarPpq)  obj->setProperty ("barPpq", b.lastBarPpq);
        if (f & hasBarCount)    obj->setProperty ("bars", (juce::int64) b.barCount);
        if (f & hasLoop)        { obj->setProperty ("loopStart", b.loopStart); obj->setProperty ("loopEnd", b.loopEnd); }
        if (f & hasTimeSamples) obj->setProperty ("samples", (juce::int64) b.timeInSamples);
        if (f & hasHostTime)    obj->setProperty ("hostNs", (juce::int64) b.hostTimeNs);
    }
    else
    {
        obj->setProperty ("offset", r.sampleOffset);

        juce::String bytes;
        for (int i = 0; i < r.midiSize; ++i)
            bytes << juce::String::toHexString ((int) r.midi[i]).paddedLeft ('0', 2) << (i + 1 < r.midiSize ? " " : "");
        obj->setProperty ("bytes", bytes);

        if (r.midiSize > 0)
        {
            const auto msg = juce::MidiMessage (r.midi, r.midiSize);

            if (msg.isNoteOnOrOff())
            {
                obj->setProperty ("kind", msg.isNoteOn() ? "on" : "off");
                obj->setProperty ("note", msg.getNoteNumber());
                obj->setProperty ("vel", (int) msg.getVelocity());
                obj->setProperty ("ch", msg.getChannel());
            }
        }

        if (r.hasEventPpq)
            obj->setProperty ("ppq", r.eventPpq);
    }

    return juce::var (obj);
}
