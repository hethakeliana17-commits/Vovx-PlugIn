cmake_minimum_required(VERSION 3.22)

project(VocalTune VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build for both Intel and Apple Silicon Macs (ignored on Windows/Linux).
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)

# Download JUCE (the plugin framework) automatically from GitHub.
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.12
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# Plugin formats: VST3 everywhere, plus a standalone app (handy for trying it without a DAW).
# Audio Unit is added on macOS.
set(VOCALTUNE_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND VOCALTUNE_FORMATS AU)
endif()

juce_add_plugin(VocalTune
    COMPANY_NAME                "MyStudio"
    PLUGIN_MANUFACTURER_CODE    Mstd
    PLUGIN_CODE                 Vtun
    PRODUCT_NAME                "Vocal Tune"
    FORMATS                     ${VOCALTUNE_FORMATS}
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Fx "Pitch Shift"
    AU_MAIN_TYPE                kAudioUnitType_Effect
    MICROPHONE_PERMISSION_ENABLED TRUE
)

target_sources(VocalTune PRIVATE
    VocalEngine.h
    PluginProcessor.h
    PluginProcessor.cpp
    PluginEditor.h
    PluginEditor.cpp
)

target_compile_definitions(VocalTune PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(VocalTune
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
