cmake_minimum_required(VERSION 3.22)

project(Masterlad VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(Masterlad
    COMPANY_NAME                "Songcraft Audio"
    BUNDLE_ID                   com.songcraftaudio.masterlad
    PLUGIN_MANUFACTURER_CODE    Sngc
    PLUGIN_CODE                 Mstl
    FORMATS                     AU VST3 Standalone
    PRODUCT_NAME                "Masterlad"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    AU_MAIN_TYPE                kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD     TRUE
)

target_sources(Masterlad PRIVATE
    DspUtils.h
    MasterProcessor.cpp
    MasterProcessor.h
    PluginEditor.cpp
    PluginEditor.h
)

target_compile_definitions(Masterlad PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(Masterlad
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
