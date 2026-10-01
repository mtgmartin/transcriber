cmake_minimum_required(VERSION 3.22)

project(TRANSCRIBER VERSION 0.1.1)

# Third-party code is downloaded at configure time (pinned, hash-checked) rather than
# committed, to keep the repository and local checkout small.
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.3
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

include(cmake/Dependencies.cmake)

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
    NEEDS_WEB_BROWSER TRUE
    NEEDS_WEBVIEW2 TRUE
    NEEDS_CURL FALSE)

# The capture core has no JUCE dependency, so the unit tests below build it on its own.
set(TRANSCRIBER_CORE_SOURCES
    source/core/CaptureEngine.cpp
    source/core/CaptureModel.cpp
    source/core/Reading.cpp)

target_compile_features(Transcriber PRIVATE cxx_std_17)

target_sources(Transcriber
    PRIVATE
        source/CaptureService.cpp
        source/DiagnosticLog.cpp
        source/MidiCompare.cpp
        source/PluginEditor.cpp
        source/PluginProcessor.cpp
        ${TRANSCRIBER_CORE_SOURCES})

juce_add_binary_data(TranscriberWebData
    SOURCES
        web/index.html
        web/app.js
        web/style.css
        web/piano.mei
        web/drums.mei
        web/guitar.mei
        ${TRANSCRIBER_WEB_LIBRARIES})

target_compile_definitions(Transcriber
    PUBLIC
        JUCE_WEB_BROWSER=1
        JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Transcriber
    PRIVATE
        TranscriberWebData
        juce::juce_audio_utils
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# --- Unit tests for the capture core (run by CI with ctest) --------------------------------
enable_testing()

add_executable(TranscriberCoreTests
    tests/core/test_main.cpp
    ${TRANSCRIBER_CORE_SOURCES})

target_include_directories(TranscriberCoreTests PRIVATE source tests/core)
target_compile_features(TranscriberCoreTests PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(TranscriberCoreTests PRIVATE /W4)
else()
    target_compile_options(TranscriberCoreTests PRIVATE -Wall -Wextra)
endif()

add_test(NAME CaptureCore COMMAND TranscriberCoreTests)
