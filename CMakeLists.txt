cmake_minimum_required(VERSION 3.22)
project(Voices VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows: link the C++ runtime statically so the plug-ins run without
# the Visual C++ Redistributable installed.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Use a local JUCE checkout with -DVOICES_JUCE_DIR=/path/to/JUCE, otherwise fetch it.
set(VOICES_JUCE_DIR "" CACHE PATH "Path to an existing JUCE 8 checkout (optional)")
if(VOICES_JUCE_DIR)
    add_subdirectory(${VOICES_JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# Embedded fonts (SIL OFL 1.1 - see Resources/Fonts), shared with Decu6000
juce_add_binary_data(VoicesFonts SOURCES
    Resources/Fonts/DecuDisplay-Bold.ttf
    Resources/Fonts/DecuDisplay-Black.ttf
    Resources/Fonts/ShareTechMono-Regular.ttf)

set(VOICES_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/PatchManager.cpp
    Source/PatchManager.h
    Source/FactoryPatches.h
    Source/Parameters.h
    Source/DSP/DspUtils.h
    Source/DSP/Formants.h
    Source/DSP/PitchDetector.h
    Source/DSP/PitchShifter.h
    Source/DSP/PitchCorrector.h
    Source/DSP/ChoirSynth.h
    Source/DSP/VocalSynth.h
    Source/DSP/Vocoder.h
    Source/DSP/Arpeggiator.h
    Source/DSP/Effects.h
    Source/DSP/VoicesEngine.h
    Source/UI/Theme.cpp
    Source/UI/Theme.h
    Source/UI/Widgets.cpp
    Source/UI/Widgets.h
    Source/UI/Panels.cpp
    Source/UI/Panels.h)

# Shared settings for both plug-ins
function(voices_configure target)
    target_sources(${target} PRIVATE ${VOICES_SOURCES})
    target_include_directories(${target} PRIVATE Source)
    target_compile_definitions(${target} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        # ALSA (default) plus JACK for the Standalone's audio device list;
        # JACK is loaded at runtime if installed. Turn off with -DVOICES_LINUX_AUDIO=OFF.
        option(VOICES_LINUX_AUDIO "ALSA + JACK in the Linux standalone" ON)
        if(VOICES_LINUX_AUDIO)
            target_compile_definitions(${target} PUBLIC JUCE_ALSA=1 JUCE_JACK=1)
        else()
            target_compile_definitions(${target} PUBLIC JUCE_ALSA=0 JUCE_JACK=0)
        endif()
    endif()
    target_link_libraries(${target}
        PRIVATE juce::juce_audio_utils
                VoicesFonts
        PUBLIC  juce::juce_recommended_config_flags
                juce::juce_recommended_lto_flags
                juce::juce_recommended_warning_flags)
endfunction()

# 1) Voices - the instrument (VST3 + Standalone with microphone input)
juce_add_plugin(Voices
    COMPANY_NAME "LexLongcoding"
    PLUGIN_MANUFACTURER_CODE Lxlc
    PLUGIN_CODE Vcs1
    PRODUCT_NAME "Voices"
    FORMATS VST3 Standalone
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    VST3_CATEGORIES Instrument Synth
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Voices uses the microphone for auto-tune, the vocoder and the voice changer."
    COPY_PLUGIN_AFTER_BUILD FALSE)
voices_configure(Voices)
target_compile_definitions(Voices PUBLIC VOICES_FX=0)

# 2) Voices FX - the same engine as an insert effect for vocal tracks
juce_add_plugin(VoicesFX
    COMPANY_NAME "LexLongcoding"
    PLUGIN_MANUFACTURER_CODE Lxlc
    PLUGIN_CODE Vcfx
    PRODUCT_NAME "Voices FX"
    FORMATS VST3
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    VST3_CATEGORIES Fx "Pitch Shift"
    COPY_PLUGIN_AFTER_BUILD FALSE)
voices_configure(VoicesFX)
target_compile_definitions(VoicesFX PUBLIC VOICES_FX=1)

# Tests (optional): cmake -B build -DVOICES_BUILD_TESTS=ON
#   VoicesEngineTest - DSP only (also builds without JUCE, see Tests/engine_render_test.cpp)
#   VoicesTests      - processor, editor screenshots and a VST3 host test
option(VOICES_BUILD_TESTS "Build the test programs" OFF)
if(VOICES_BUILD_TESTS)
    add_executable(VoicesEngineTest Tests/engine_render_test.cpp)
    target_include_directories(VoicesEngineTest PRIVATE Source)

    juce_add_console_app(VoicesTests PRODUCT_NAME "VoicesTests")
    target_sources(VoicesTests PRIVATE Tests/plugin_test.cpp ${VOICES_SOURCES})
    target_include_directories(VoicesTests PRIVATE Source)
    target_compile_definitions(VoicesTests PRIVATE
        VOICES_FX=0 JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        JUCE_ALSA=0 JUCE_JACK=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(VoicesTests PRIVATE juce::juce_audio_utils VoicesFonts
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
