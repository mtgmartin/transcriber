#include "PluginEditor.h"

TranscriberEditor::TranscriberEditor (TranscriberProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setSize (480, 260);
    startTimerHz (10);
}

void TranscriberEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1b2130));

    const auto s = processor.getStatus();

    auto orMissing = [] (bool present, const juce::String& text) { return present ? text : juce::String ("not provided"); };

    const juce::StringArray lines {
        juce::String ("Transcriber ") + JucePlugin_VersionString + "  (Phase 0 build)",
        "",
        "Notes received: " + juce::String (s.notesReceived)
            + (s.lastNote >= 0 ? "   last: " + juce::MidiMessage::getMidiNoteName (s.lastNote, true, true, 3)
                                   + " (" + juce::String (s.lastNote) + ")"
                               : juce::String()),
        juce::String ("Transport: ") + (s.isPlaying ? "playing" : "stopped"),
        "Position (quarter notes): " + orMissing (s.hasPpq, juce::String (s.ppq, 3)),
        "Tempo: " + orMissing (s.hasBpm, juce::String (s.bpm, 2) + " BPM"),
        "Time signature: " + orMissing (s.hasTimeSig, juce::String (s.timeSigNumerator) + "/" + juce::String (s.timeSigDenominator)),
        "Sample rate: " + juce::String (s.sampleRate, 0) + " Hz"
    };

    g.setColour (juce::Colours::white);
    g.setFont (16.0f);

    auto area = getLocalBounds().reduced (20);
    for (const auto& line : lines)
        g.drawText (line, area.removeFromTop (26), juce::Justification::centredLeft);
}
