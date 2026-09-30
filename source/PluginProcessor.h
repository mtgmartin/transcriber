#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

// Phase 0: an instrument that outputs silence and reports what it receives.
// The audio thread only writes atomics; the editor polls them.
class TranscriberProcessor final : public juce::AudioProcessor
{
public:
    TranscriberProcessor();
    ~TranscriberProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    // Some hosts misbehave when a plugin reports zero programs.
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    struct Status
    {
        int notesReceived = 0;
        int lastNote = -1;
        bool isPlaying = false;
        bool hasPpq = false;
        double ppq = 0.0;
        bool hasBpm = false;
        double bpm = 0.0;
        bool hasTimeSig = false;
        int timeSigNumerator = 0;
        int timeSigDenominator = 0;
        double sampleRate = 0.0;
    };

    Status getStatus() const;

private:
    std::atomic<int> notesReceived { 0 };
    std::atomic<int> lastNote { -1 };
    std::atomic<bool> isPlaying { false };
    std::atomic<bool> hasPpq { false };
    std::atomic<double> ppq { 0.0 };
    std::atomic<bool> hasBpm { false };
    std::atomic<double> bpm { 0.0 };
    std::atomic<bool> hasTimeSig { false };
    std::atomic<int> timeSigNumerator { 0 };
    std::atomic<int> timeSigDenominator { 0 };
    std::atomic<double> currentSampleRate { 0.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TranscriberProcessor)
};
