#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Capture.h"
#include "DiagnosticLog.h"

#include <atomic>

// Phase 1 diagnostic build: an instrument that outputs silence, logs everything
// the host sends, and captures notes for the timing comparison.
class TranscriberProcessor final : public juce::AudioProcessor
{
public:
    TranscriberProcessor();
    ~TranscriberProcessor() override;

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

    //==============================================================================
    const juce::String instanceId;
    DiagnosticLog& getLog() { return log; }
    Capture& getCapture() { return capture; }

    // Latest transport info, for the editor's status display.
    DiagnosticLog::BlockInfo getLatestBlockInfo() const;
    double getSampleRateForDisplay() const noexcept { return currentSampleRate.load(); }

    // Test 1.7: fills the saved state with this many megabytes of checkable data.
    void setStateTestSize (int megabytes);
    int getStateTestSize() const;
    juce::String getLastStateCheck() const;

private:
    static void fillTestData (juce::MemoryBlock&, size_t numBytes, juce::int64 seed);

    Capture capture;
    DiagnosticLog log;

    std::atomic<double> currentSampleRate { 0.0 };
    uint64_t blockIndex = 0;                  // audio thread only
    DiagnosticLog::BlockInfo previousInfo;   // audio thread only
    bool havePreviousInfo = false;           // audio thread only

    mutable juce::SpinLock latestLock;
    DiagnosticLog::BlockInfo latestInfo;

    mutable std::mutex stateMutex;
    int stateTestMegabytes = 0;
    juce::int64 stateTestSeed = 0;
    juce::String lastStateCheck { "No state has been restored in this session." };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TranscriberProcessor)
};
