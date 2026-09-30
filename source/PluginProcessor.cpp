#include "PluginProcessor.h"
#include "PluginEditor.h"

TranscriberProcessor::TranscriberProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

void TranscriberProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate.store (sampleRate);
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

    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();

        if (message.isNoteOn())
        {
            notesReceived.fetch_add (1);
            lastNote.store (message.getNoteNumber());
        }
    }

    // Never pass MIDI through: the plugin declares no MIDI output.
    midi.clear();

    if (auto* playHead = getPlayHead())
    {
        if (const auto position = playHead->getPosition())
        {
            isPlaying.store (position->getIsPlaying());

            const auto positionPpq = position->getPpqPosition();
            hasPpq.store (positionPpq.hasValue());
            if (positionPpq.hasValue())
                ppq.store (*positionPpq);

            const auto tempo = position->getBpm();
            hasBpm.store (tempo.hasValue());
            if (tempo.hasValue())
                bpm.store (*tempo);

            const auto timeSig = position->getTimeSignature();
            hasTimeSig.store (timeSig.hasValue());
            if (timeSig.hasValue())
            {
                timeSigNumerator.store (timeSig->numerator);
                timeSigDenominator.store (timeSig->denominator);
            }
        }
    }
}

TranscriberProcessor::Status TranscriberProcessor::getStatus() const
{
    Status s;
    s.notesReceived = notesReceived.load();
    s.lastNote = lastNote.load();
    s.isPlaying = isPlaying.load();
    s.hasPpq = hasPpq.load();
    s.ppq = ppq.load();
    s.hasBpm = hasBpm.load();
    s.bpm = bpm.load();
    s.hasTimeSig = hasTimeSig.load();
    s.timeSigNumerator = timeSigNumerator.load();
    s.timeSigDenominator = timeSigDenominator.load();
    s.sampleRate = currentSampleRate.load();
    return s;
}

juce::AudioProcessorEditor* TranscriberProcessor::createEditor()
{
    return new TranscriberEditor (*this);
}

// State is added in Phase 3.
void TranscriberProcessor::getStateInformation (juce::MemoryBlock&) {}
void TranscriberProcessor::setStateInformation (const void*, int) {}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TranscriberProcessor();
}
