#pragma once

#include "PluginProcessor.h"

#include <juce_gui_extra/juce_gui_extra.h>

// A WebView2 page (bundled in the plugin): the capture controls and note preview, plus the
// Phase 1 test tools (Verovio test scores, PDF export, key presses, host status).
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
    uint64_t lastPreviewRevision = ~(uint64_t) 0;
    double lastPreviewMs = 0.0;
    const bool browserSupported;
    juce::WebBrowserComponent browser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TranscriberEditor)
};
