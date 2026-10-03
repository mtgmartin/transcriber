#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "CaptureService.h"
#include "DiagnosticLog.h"

#include <atomic>

// An instrument that outputs silence and captures the MIDI it receives (see CaptureService).
// The diagnostic log of everything the host sends is off unless switched on.
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
    CaptureService& getCapture() { return capture; }

    // Switches the diagnostic log on or off; switching it on writes the host and prepare info first.
    void setDiagnosticsEnabled (bool);

    // Latest transport info, for the editor's status display.
    DiagnosticLog::BlockInfo getLatestBlockInfo() const;
    double getSampleRateForDisplay() const noexcept { return currentSampleRate.load(); }

    // The latest note-on seen (for learning drum notes), and how many there have been.
    int getLastNote() const noexcept { return lastNote.load(); }
    uint32_t getNoteCount() const noexcept { return noteCount.load(); }

private:
    void logHostInfo();

    CaptureService capture;
    DiagnosticLog log;

    std::atomic<double> currentSampleRate { 0.0 };
    std::atomic<int> lastNote { -1 };
    std::atomic<uint32_t> noteCount { 0 };
    std::atomic<int> maxBlockSize { 0 };
    std::atomic<bool> nonRealtimePrepared { false };
    uint64_t blockIndex = 0;                  // audio thread only
    DiagnosticLog::BlockInfo previousInfo;   // audio thread only
    bool havePreviousInfo = false;           // audio thread only

    mutable juce::SpinLock latestLock;
    DiagnosticLog::BlockInfo latestInfo;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TranscriberProcessor)
};
