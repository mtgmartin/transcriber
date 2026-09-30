cmake_minimum_required(VERSION 3.22)

project(TRANSCRIBER VERSION 0.0.1)

# JUCE is fetched at configure time (not vendored) to keep the local checkout small.
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.3
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# The manufacturer and plugin codes identify the plugin inside saved Live Sets.
# Never change them, or existing Sets will no longer find the plugin.
juce_add_plugin(Transcriber
    COMPANY_NAME "mtgmartin"
    IS_SYNTH TRUE                  # Live 11 only loads MIDI-receiving VST3s declared as instruments
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE        # the plugin never sends MIDI back to Live
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE Mtgm
    PLUGIN_CODE Trsc
    FORMATS VST3
    VST3_CATEGORIES Instrument Tools
    PRODUCT_NAME "Transcriber"
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE)

target_compile_features(Transcriber PRIVATE cxx_std_17)

target_sources(Transcriber
    PRIVATE
        source/PluginProcessor.cpp
        source/PluginEditor.cpp)

target_compile_definitions(Transcriber
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Transcriber
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
