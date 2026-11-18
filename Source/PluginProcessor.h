// Voices - Copyright (C) 2026 LexLongcoding
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "DSP/VoicesEngine.h"
#include "Parameters.h"
#include "PatchManager.h"
#include <array>
#include <atomic>

// Two builds share this code:
//   Voices     (instrument) - play the choir / vocal synth / vocoder from MIDI; feed a
//                             voice into its side-chain input for the vocoder & auto-tune.
//   Voices FX  (effect, VOICES_FX=1) - insert on a vocal track for auto-tune and the
//                             voice changer; it also takes MIDI for the vocoder / synths.
class VoicesAudioProcessor : public juce::AudioProcessor
{
public:
    VoicesAudioProcessor();
    ~VoicesAudioProcessor() override = default;

    static constexpr bool isFxBuild =
       #if VOICES_FX
        true;
       #else
        false;
       #endif

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return isFxBuild ? "Voices FX" : "Voices"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }

    // Patches are chosen in the plug-in's own browser, not exposed as host programs
    // (some hosts re-send program 0 on transport stop - see Decu6000 0.9.1).
    int getNumPrograms() override                         { return 1; }
    int getCurrentProgram() override                      { return 0; }
    void setCurrentProgram (int) override                 {}
    const juce::String getProgramName (int) override      { return getName(); }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static int paramIndexForId (const juce::String& id);
    static int findParamIndex (const juce::String& id);   // -1 if unknown (no assert)

    juce::AudioProcessorValueTreeState apvts;
    PatchManager patches { apvts };
    juce::MidiKeyboardState keyboardState;   // the on-screen keyboard

    //==============================================================================
    // MIDI learn (CC -> parameter), ported from Decu6000. Parameters are addressed by
    // their index in Params::specs. Lock-free: armed / cleared from the message thread,
    // applied on the audio thread. Saved with the session, not with patches.
    static constexpr int numCcs = 120;    // CC 120-127 are channel-mode messages, never mapped
    static constexpr int sustainCc = 64;  // stays the sustain pedal, never mapped

    void armCcLearn (int paramIndex)    { learnCcParam.store (paramIndex); }   // -1 cancels
    void cancelLearning()               { learnCcParam.store (-1); }
    int  getArmedCcParam() const        { return learnCcParam.load(); }
    int  getCcLearnCount() const        { return ccLearnCount.load(); }
    int  getLastLearnedCc() const       { return lastLearnedCc.load(); }
    int  ccForParam (int paramIndex) const;          // -1 if none
    int  paramForCc (int cc) const;                  // -1 if none
    void clearCcForParam (int paramIndex);
    void clearAllCcs();
    int  getNumMappedCcs() const;

    //==============================================================================
    // Read-outs for the editor (lock-free).
    struct Readout
    {
        std::atomic<float> inputMidi { 0.0f }, outputMidi { 0.0f };
        std::atomic<int>   targetNote { -1 }, arpStep { 0 };
        std::atomic<bool>  voiced { false };
        std::atomic<float> inLevel { 0.0f };
        std::array<std::atomic<float>, 2> out {};
        std::array<std::atomic<float>, Vocoder::maxBands> bands {};
        std::atomic<int>   numBands { 20 };
        std::atomic<bool>  hasInput { false };
    } readout;

    int getLatencyMs() const { return juce::roundToInt (engine.getLatency() * 1000.0 / juce::jmax (1.0, getSampleRate())); }

    // Editor state saved with the session (message thread only).
    float uiScale = 0.8f;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static BusesProperties makeBuses();
    float pv (int index) const { return raw[static_cast<size_t> (index)]->load(); }
    void updateSettings();
    void handleController (int cc, int value);
    juce::String ccMapToString() const;
    void ccMapFromString (const juce::String&);

    std::array<std::atomic<float>*, static_cast<size_t> (Params::numParams)> raw {};
    std::array<juce::RangedAudioParameter*, static_cast<size_t> (Params::numParams)> paramList {};

    struct Ix
    {
        int masterLevel, voiceType, voiceLevel;
        int tuneOn, tuneKey, tuneScale, tuneSource, tuneRetune, tuneAmount, tuneHumanize;
        int chgOn, chgAmount, chgPitch, chgFormant, chgWidth;
        int vocOn, vocBands, vocCarrier, vocRelease, vocFormant, vocSibilance, vocAuto, vocLevel;
        int choirOn, choirVowel, choirSize, choirBreath, choirVibrato, choirAttack, choirRelease, choirLevel;
        int vsynOn, vsynVowel, vsynVibrato, vsynBreath, vsynBright, vsynGlide, vsynAttack, vsynRelease, vsynLevel;
        int arpOn, arpMode, arpOctaves, arpRate, arpGate, arpLatch;
        int chorusRate, chorusDepth, chorusMix, delayTime, delayFeedback, delayMix, reverbSize, reverbDamp, reverbMix;
    } ix {};

    VoicesEngine engine;
    std::vector<VoicesEngine::MidiEvent> events;
    std::vector<float> monoIn;
    std::array<bool, 128> keyDown {}, sustained {};
    bool sustainPedal = false;

    std::atomic<int> learnCcParam { -1 }, ccLearnCount { 0 }, lastLearnedCc { -1 };
    std::array<std::atomic<int>, numCcs> ccToParam;

    static constexpr const char* ccMapProperty = "ccMap";
    static constexpr const char* uiScaleProperty = "uiScale";
    static constexpr const char* patchNameProperty = "patchName";
    static constexpr const char* patchModifiedProperty = "patchModified";

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoicesAudioProcessor)
};
