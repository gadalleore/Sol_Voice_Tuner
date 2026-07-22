cmake_minimum_required(VERSION 3.22)

project(SolVoiceTuner VERSION 1.0.0 LANGUAGES C CXX)

# ----------------------------------------------------------------------------
# Project-wide settings
# ----------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Use folders in IDEs (Visual Studio / Xcode)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Speed up Debug builds for the heavy DSP libs (Stretch is ~10x slower in Debug)
if(MSVC)
    add_compile_options(/MP)               # multi-process compilation
endif()

# ----------------------------------------------------------------------------
# Dependencies
# ----------------------------------------------------------------------------
# We pre-clone JUCE, signalsmith-stretch, and signalsmith-linear into
# external/ at project root via build-and-launch.ps1. Using local copies
# avoids GitHub network hiccups during configure on flaky connections.
#
# If you prefer FetchContent's git mode, delete the external/ folder and
# this block will fall back to cloning automatically.
# ----------------------------------------------------------------------------
include(FetchContent)

set(EXTERNAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external")

# JUCE -----------------------------------------------------------------------
if(EXISTS "${EXTERNAL_DIR}/JUCE/CMakeLists.txt")
    message(STATUS "Using pre-cloned JUCE at ${EXTERNAL_DIR}/JUCE")
    add_subdirectory("${EXTERNAL_DIR}/JUCE" "${CMAKE_BINARY_DIR}/_juce_build" EXCLUDE_FROM_ALL)
else()
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.4
        GIT_SHALLOW    ON
    )
    FetchContent_MakeAvailable(JUCE)
endif()

# signalsmith-linear (must be declared BEFORE signalsmith-stretch so the
# nested FetchContent inside stretch's CMakeLists.txt sees the existing target)
if(EXISTS "${EXTERNAL_DIR}/signalsmith-linear/CMakeLists.txt"
   AND NOT TARGET signalsmith-linear)
    message(STATUS "Using pre-cloned signalsmith-linear")
    add_subdirectory("${EXTERNAL_DIR}/signalsmith-linear"
                     "${CMAKE_BINARY_DIR}/_signalsmith_linear_build" EXCLUDE_FROM_ALL)
endif()

# signalsmith-stretch --------------------------------------------------------
if(EXISTS "${EXTERNAL_DIR}/signalsmith-stretch/CMakeLists.txt")
    message(STATUS "Using pre-cloned signalsmith-stretch")
    add_subdirectory("${EXTERNAL_DIR}/signalsmith-stretch"
                     "${CMAKE_BINARY_DIR}/_signalsmith_stretch_build" EXCLUDE_FROM_ALL)
else()
    FetchContent_Declare(
        signalsmith-stretch
        GIT_REPOSITORY https://github.com/Signalsmith-Audio/signalsmith-stretch.git
        GIT_TAG        main
        GIT_SHALLOW    ON
    )
    FetchContent_MakeAvailable(signalsmith-stretch)
endif()

# ----------------------------------------------------------------------------
# Plugin definition
# ----------------------------------------------------------------------------
juce_add_plugin(SolVoiceTuner
    COMPANY_NAME              "63C"
    PLUGIN_MANUFACTURER       "63C"
    BUNDLE_ID                 "com.63c.solvoicetuner"
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          TRUE     # MIDI for target-note control
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD   FALSE
    PLUGIN_MANUFACTURER_CODE  S63c
    PLUGIN_CODE               Svtn
    FORMATS                   VST3 Standalone
    PLUGIN_NAME               "Sol Voice Tuner"
    PRODUCT_NAME              "Sol Voice Tuner"
    DESCRIPTION               "Real-time AutoTune-style pitch corrector"
    VST3_CATEGORIES           Fx Pitch Shift
    AU_MAIN_TYPE              kAudioUnitType_Effect
)

# Generate the legacy JuceHeader.h umbrella include that our source files use.
juce_generate_juce_header(SolVoiceTuner)

# ----------------------------------------------------------------------------
# Source files
# ----------------------------------------------------------------------------
target_sources(SolVoiceTuner PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/PageStack.h
    Source/SolPage.h
    Source/HomePage.h
    Source/EffectsWindowPage.h
    Source/HarmoniesWindowPage.h
    Source/TuningWindowPage.h
    Source/LegacyTunerPage.cpp
    Source/LegacyTunerPage.h
    Source/PitchDetector.cpp
    Source/PitchDetector.h
    Source/ScaleQuantizer.h
    Source/PitchShifter.cpp
    Source/PitchShifter.h
    Source/SolLookAndFeel.h
    Source/VocalFx.h
    Source/EffectChain.h
    Source/OscilloscopeComponent.cpp
    Source/OscilloscopeComponent.h
)

# ----------------------------------------------------------------------------
# Compile-time defines required by JUCE plugins
# ----------------------------------------------------------------------------
target_compile_definitions(SolVoiceTuner PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_USE_OGGVORBIS=0
)

# ----------------------------------------------------------------------------
# Link JUCE modules + Signalsmith Stretch
# ----------------------------------------------------------------------------
target_link_libraries(SolVoiceTuner PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_plugin_client
    juce::juce_audio_formats
    juce::juce_gui_extra
    juce::juce_gui_basics
    juce::juce_graphics
    juce::juce_events
    juce::juce_core
    juce::juce_data_structures
    signalsmith-stretch
PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# ----------------------------------------------------------------------------
# Make sure Stretch builds at full speed even in Debug
# ----------------------------------------------------------------------------
if(MSVC)
    target_compile_options(SolVoiceTuner PRIVATE
        $<$<CONFIG:Release>:/O2 /fp:fast>
    )
else()
    target_compile_options(SolVoiceTuner PRIVATE
        $<$<CONFIG:Release>:-O3 -ffast-math>
    )
endif()

# ----------------------------------------------------------------------------
# Group sources for IDEs
# ----------------------------------------------------------------------------
source_group("Source" REGULAR_EXPRESSION "Source/.*")

# ----------------------------------------------------------------------------
# Dev helper: print chromatic root labels (no JUCE; run from build/Release)
# ----------------------------------------------------------------------------
add_executable (SolVoiceTuner_KeyDumpConsole EXCLUDE_FROM_ALL
    tools/KeyDumpConsole.cpp)
target_include_directories (SolVoiceTuner_KeyDumpConsole PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")
