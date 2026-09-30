#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cstring>

static juce::String makeInstanceId()
{
    return juce::String::toHexString (juce::Random::getSystemRandom().nextInt (0x10000)).paddedLeft ('0', 4).toUpperCase();
}

TranscriberProcessor::TranscriberProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      instanceId (makeInstanceId()),
      log (instanceId, [this] (const DiagnosticLog::Record& r) { capture.add (r); })
{
    auto* info = new juce::DynamicObject();
    info->setProperty ("plugin", juce::String (JucePlugin_Name) + " " + JucePlugin_VersionString);
    info->setProperty ("instance", instanceId);
    info->setProperty ("host", juce::PluginHostType().getHostDescription());
    info->setProperty ("hostPath", juce::File::getSpecialLocation (juce::File::hostApplicationPath).getFullPathName());
    info->setProperty ("wrapper", juce::AudioProcessor::getWrapperTypeDescription (wrapperType));
    info->setProperty ("os", juce::SystemStats::getOperatingSystemName());
    log.logEvent ("start", juce::var (info));
}

TranscriberProcessor::~TranscriberProcessor()
{
    log.logEvent ("shutdown");
}

void TranscriberProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate.store (sampleRate);

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

    if (auto* playHead = getPlayHead())
    {
        if (const auto pos = playHead->getPosition())
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

    const auto wallMs = juce::Time::getMillisecondCounterHiRes();
    const auto isPlaying = (info.flags & Log::playing) != 0;

    // Every block while playing; otherwise only when the transport state changes.
    if (isPlaying || ! havePreviousInfo || ! info.sameTransportAs (previousInfo) || ! midi.isEmpty())
    {
        Log::Record r;
        r.kind = Log::Record::Kind::block;
        r.blockIndex = blockIndex;
        r.wallMs = wallMs;
        r.numSamples = buffer.getNumSamples();
        r.info = info;
        log.push (r);
    }

    const auto sr = currentSampleRate.load();

    for (const auto metadata : midi)
    {
        Log::Record r;
        r.kind = Log::Record::Kind::midi;
        r.blockIndex = blockIndex;
        r.wallMs = wallMs;
        r.numSamples = buffer.getNumSamples();
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
// State format for the Phase 1 test: "TRS1", megabytes, seed, then the test data.
static constexpr juce::uint32 stateMagic = 0x31535254;   // "TRS1" little-endian

void TranscriberProcessor::fillTestData (juce::MemoryBlock& block, size_t numBytes, juce::int64 seed)
{
    block.setSize (numBytes);
    juce::Random random (seed);
    auto* data = static_cast<juce::uint8*> (block.getData());

    for (size_t i = 0; i < numBytes; ++i)
        data[i] = (juce::uint8) random.nextInt (256);
}

void TranscriberProcessor::setStateTestSize (int megabytes)
{
    const std::lock_guard<std::mutex> lock (stateMutex);
    stateTestMegabytes = juce::jlimit (0, 64, megabytes);
    stateTestSeed = juce::Time::currentTimeMillis();
    log.logEvent ("stateTestSize", stateTestMegabytes);
}

int TranscriberProcessor::getStateTestSize() const
{
    const std::lock_guard<std::mutex> lock (stateMutex);
    return stateTestMegabytes;
}

juce::String TranscriberProcessor::getLastStateCheck() const
{
    const std::lock_guard<std::mutex> lock (stateMutex);
    return lastStateCheck;
}

void TranscriberProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const auto start = juce::Time::getMillisecondCounterHiRes();

    int megabytes;
    juce::int64 seed;
    {
        const std::lock_guard<std::mutex> lock (stateMutex);
        megabytes = stateTestMegabytes;
        seed = stateTestSeed;
    }

    juce::MemoryOutputStream out (destData, false);
    out.writeInt ((int) stateMagic);
    out.writeInt (megabytes);
    out.writeInt64 (seed);

    if (megabytes > 0)
    {
        juce::MemoryBlock test;
        fillTestData (test, (size_t) megabytes * 1024 * 1024, seed);
        out.write (test.getData(), test.getSize());
    }

    out.flush();

    auto* info = new juce::DynamicObject();
    info->setProperty ("bytes", (juce::int64) destData.getSize());
    info->setProperty ("ms", juce::Time::getMillisecondCounterHiRes() - start);
    log.logEvent ("getState", juce::var (info));
}

void TranscriberProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto start = juce::Time::getMillisecondCounterHiRes();
    juce::MemoryInputStream in (data, (size_t) sizeInBytes, false);

    juce::String check;
    int megabytes = 0;
    juce::int64 seed = 0;

    if (sizeInBytes < 16 || (juce::uint32) in.readInt() != stateMagic)
    {
        check = "Restored state was not recognised (" + juce::String (sizeInBytes) + " bytes).";
    }
    else
    {
        megabytes = in.readInt();
        seed = in.readInt64();
        const auto expectedSize = (size_t) megabytes * 1024 * 1024;
        const auto available = (size_t) in.getNumBytesRemaining();

        if (available != expectedSize)
        {
            check = "FAILED: expected " + juce::String ((juce::int64) expectedSize) + " test bytes, got " + juce::String ((juce::int64) available) + ".";
        }
        else if (megabytes == 0)
        {
            check = "Restored OK (no test data).";
        }
        else
        {
            juce::MemoryBlock expected;
            fillTestData (expected, expectedSize, seed);
            const auto* actual = static_cast<const juce::uint8*> (data) + 16;
            check = std::memcmp (expected.getData(), actual, expectedSize) == 0
                        ? "Restored OK: " + juce::String (megabytes) + " MB of test data matches."
                        : "FAILED: " + juce::String (megabytes) + " MB of test data does not match.";
        }
    }

    {
        const std::lock_guard<std::mutex> lock (stateMutex);
        stateTestMegabytes = megabytes;
        stateTestSeed = seed;
        lastStateCheck = check;
    }

    auto* info = new juce::DynamicObject();
    info->setProperty ("bytes", sizeInBytes);
    info->setProperty ("ms", juce::Time::getMillisecondCounterHiRes() - start);
    info->setProperty ("result", check);
    log.logEvent ("setState", juce::var (info));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TranscriberProcessor();
}
