#pragma once

#include "PluginProcessor.h"

class TranscriberEditor final : public juce::AudioProcessorEditor,
                                private juce::Timer
{
public:
    explicit TranscriberEditor (TranscriberProcessor&);
    ~TranscriberEditor() override = default;

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override { repaint(); }

    TranscriberProcessor& processor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TranscriberEditor)
};
