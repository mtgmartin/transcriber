#pragma once

#include "PluginProcessor.h"

#include <juce_gui_extra/juce_gui_extra.h>

// Phase 1: a WebView2 page (bundled in the plugin) that renders Verovio test scores,
// exports a test PDF, records key presses, and shows the diagnostic status.
class TranscriberEditor final : public juce::AudioProcessorEditor,
                                private juce::Timer
{
public:
    explicit TranscriberEditor (TranscriberProcessor&);
    ~TranscriberEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    juce::WebBrowserComponent::Options makeBrowserOptions();
    static std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url);

    void compareWithMidiFile();
    void savePdf (const juce::var& payload);

    TranscriberProcessor& processor;
    std::unique_ptr<juce::FileChooser> chooser;
    const bool browserSupported;
    juce::WebBrowserComponent browser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TranscriberEditor)
};
