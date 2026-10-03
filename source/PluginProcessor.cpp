#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <array>
#include <cstring>

// More MIDI events than this in one audio block are dropped (and counted).
static constexpr size_t maxMidiEventsPerBlock = 1024;

static juce::String makeInstanceId()
{
    return juce::String::toHexString (juce::Random::getSystemRandom().nextInt (0x10000)).paddedLeft ('0', 4).toUpperCase();
}

TranscriberProcessor::TranscriberProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      instanceId (makeInstanceId()),
      log (instanceId)
{
    logHostInfo();
}

void TranscriberProcessor::logHostInfo()
{
    auto* info = new juce::DynamicObject();
    info->setProperty ("plugin", juce::String (JucePlugin_Name) + " " + JucePlugin_VersionString);
    info->setProperty ("instance", instanceId);
    info->setProperty ("host", juce::PluginHostType().getHostDescription());
    info->setProperty ("hostPath", juce::File::getSpecialLocation (juce::File::hostApplicationPath).getFullPathName());
    info->setProperty ("wrapper", juce::AudioProcessor::getWrapperTypeDescription (wrapperType));
    info->setProperty ("os", juce::SystemStats::getOperatingSystemName());
    log.logEvent ("start", juce::var (info));

    auto* prepared = new juce::DynamicObject();
    prepared->setProperty ("sampleRate", currentSampleRate.load());
    prepared->setProperty ("maxBlock", maxBlockSize.load());
    prepared->setProperty ("offline", nonRealtimePrepared.load());
    log.logEvent ("prepare", juce::var (prepared));
}

void TranscriberProcessor::setDiagnosticsEnabled (bool shouldLog)
{
    log.setEnabled (shouldLog);

    if (shouldLog)
        logHostInfo();
}

TranscriberProcessor::~TranscriberProcessor()
{
    log.logEvent ("shutdown");
}

void TranscriberProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate.store (sampleRate);
    maxBlockSize.store (samplesPerBlock);
    nonRealtimePrepared.store (isNonRealtime());

    auto* info = new juce::DynamicObject();
    info->setProperty ("sampleRate", sampleRate);
    info->setProperty ("maxBlock", samplesPerBlock);
    info->setProperty ("offline", isNonRealtime());
    log.logEvent ("prepare", juce::var (info));
}

bool TranscriberProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void TranscriberProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // Transcriber never makes sound.
    buffer.clear();

    using Log = DiagnosticLog;
    Log::BlockInfo info;

    if (isNonRealtime())
        info.flags |= Log::nonRealtime;

    if (auto* hostPlayHead = getPlayHead())
    {
        if (const auto pos = hostPlayHead->getPosition())
        {
            info.flags |= Log::hasPosition;

            if (pos->getIsPlaying())   info.flags |= Log::playing;
            if (pos->getIsLooping())   info.flags |= Log::looping;
            if (pos->getIsRecording()) info.flags |= Log::recording;

            if (const auto v = pos->getPpqPosition())             { info.flags |= Log::hasPpq;         info.ppq = *v; }
            if (const auto v = pos->getBpm())                     { info.flags |= Log::hasBpm;         info.bpm = *v; }
            if (const auto v = pos->getTimeSignature())           { info.flags |= Log::hasTimeSig;     info.tsNum = v->numerator; info.tsDen = v->denominator; }
            if (const auto v = pos->getPpqPositionOfLastBarStart()) { info.flags |= Log::hasLastBarPpq; info.lastBarPpq = *v; }
            if (const auto v = pos->getBarCount())                { info.flags |= Log::hasBarCount;    info.barCount = *v; }
            if (const auto v = pos->getLoopPoints())              { info.flags |= Log::hasLoop;        info.loopStart = v->ppqStart; info.loopEnd = v->ppqEnd; }
            if (const auto v = pos->getTimeInSamples())           { info.flags |= Log::hasTimeSamples; info.timeInSamples = *v; }
            if (const auto v = pos->getHostTimeNs())              { info.flags |= Log::hasHostTime;    info.hostTimeNs = *v; }
        }
    }

    const auto sr = currentSampleRate.load();
    const auto numSamples = buffer.getNumSamples();

    // The capture engine gets the position, the tempo and the note messages.
    {
        trs::HostBlock hostBlock;
        hostBlock.playing     = (info.flags & Log::playing) != 0;
        hostBlock.looping     = (info.flags & Log::looping) != 0;
        hostBlock.nonRealtime = (info.flags & Log::nonRealtime) != 0;
        hostBlock.hasPpq      = (info.flags & Log::hasPpq) != 0;
        hostBlock.hasBpm      = (info.flags & Log::hasBpm) != 0;
        hostBlock.hasTimeSig  = (info.flags & Log::hasTimeSig) != 0;
        hostBlock.hasBarStart = (info.flags & Log::hasLastBarPpq) != 0;
        hostBlock.ppq         = info.ppq;
        hostBlock.bpm         = info.bpm;
        hostBlock.barStartPpq = info.lastBarPpq;
        hostBlock.tsNum       = info.tsNum;
        hostBlock.tsDen       = info.tsDen;
        hostBlock.numSamples  = numSamples;
        hostBlock.sampleRate  = sr;

        std::array<trs::MidiEvent, maxMidiEventsPerBlock> events;
        int numEvents = 0;
        uint32_t tooMany = 0;

        for (const auto metadata : midi)
        {
            if (numEvents >= (int) events.size())
            {
                ++tooMany;
                continue;
            }

            if (metadata.numBytes >= 3 && (metadata.data[0] & 0xF0) == 0x90 && metadata.data[2] > 0)
            {
                lastNote.store (metadata.data[1]);
                noteCount.fetch_add (1);
            }

            auto& e = events[(size_t) numEvents++];
            e.sampleOffset = metadata.samplePosition;
            e.size = juce::jmin (metadata.numBytes, 3);
            std::memcpy (e.data, metadata.data, (size_t) e.size);
        }

        if (tooMany > 0)
            capture.getEngine().addDropped (tooMany);

        capture.getEngine().process (hostBlock, events.data(), numEvents);
    }

    if (log.isEnabled())
    {
        const auto wallMs = juce::Time::getMillisecondCounterHiRes();
        const auto isPlaying = (info.flags & Log::playing) != 0;

        // Every block while playing; otherwise only when the transport state changes.
        if (isPlaying || ! havePreviousInfo || ! info.sameTransportAs (previousInfo) || ! midi.isEmpty())
        {
            Log::Record r;
            r.kind = Log::Record::Kind::block;
            r.blockIndex = blockIndex;
            r.wallMs = wallMs;
            r.numSamples = numSamples;
            r.info = info;
            log.push (r);
        }

        for (const auto metadata : midi)
        {
            Log::Record r;
            r.kind = Log::Record::Kind::midi;
            r.blockIndex = blockIndex;
            r.wallMs = wallMs;
            r.numSamples = numSamples;
            r.info = info;
            r.sampleOffset = metadata.samplePosition;
            r.midiSize = juce::jmin (metadata.numBytes, 3);
            std::memcpy (r.midi, metadata.data, (size_t) r.midiSize);

            if ((info.flags & Log::hasPpq) && (info.flags & Log::hasBpm) && sr > 0.0)
            {
                r.hasEventPpq = true;
                r.eventPpq = info.ppq + (metadata.samplePosition / sr) * (info.bpm / 60.0);
            }

            log.push (r);
        }
    }

    // Never pass MIDI through: the plugin declares no MIDI output.
    midi.clear();

    previousInfo = info;
    havePreviousInfo = true;
    ++blockIndex;

    const juce::SpinLock::ScopedTryLockType lock (latestLock);

    if (lock.isLocked())
        latestInfo = info;
}

DiagnosticLog::BlockInfo TranscriberProcessor::getLatestBlockInfo() const
{
    const juce::SpinLock::ScopedLockType lock (latestLock);
    return latestInfo;
}

juce::AudioProcessorEditor* TranscriberProcessor::createEditor()
{
    return new TranscriberEditor (*this);
}

//==============================================================================
// The host keeps whatever these two functions produce: the versions, as compressed JSON (see StateCodec.h).
void TranscriberProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const auto start = juce::Time::getMillisecondCounterHiRes();
    capture.saveState (destData);

    auto* info = new juce::DynamicObject();
    info->setProperty ("bytes", (juce::int64) destData.getSize());
    info->setProperty ("ms", juce::Time::getMillisecondCounterHiRes() - start);
    log.logEvent ("getState", juce::var (info));
}

void TranscriberProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto start = juce::Time::getMillisecondCounterHiRes();
    capture.loadState (data, (size_t) juce::jmax (0, sizeInBytes));

    auto* info = new juce::DynamicObject();
    info->setProperty ("bytes", sizeInBytes);
    info->setProperty ("ms", juce::Time::getMillisecondCounterHiRes() - start);
    info->setProperty ("versions", capture.getNumVersions());
    log.logEvent ("setState", juce::var (info));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TranscriberProcessor();
}
