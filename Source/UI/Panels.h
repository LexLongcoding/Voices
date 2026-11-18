// Voices - Copyright (C) 2026 LexLongcoding
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
#include "Widgets.h"
#include <array>

// Live pitch trace: what you sing (cyan) and what comes out (pink), on a note grid.
class PitchDisplay : public juce::Component
{
public:
    explicit PitchDisplay (UiContext& c) : ctx (c) {}
    void paint (juce::Graphics&) override;
    void push();   // ~30 Hz

private:
    UiContext& ctx;
    static constexpr int historySize = 220;
    std::array<float, historySize> in {}, out {};
    int head = 0;
    float centre = 57.0f;
    int lastTarget = -1;
    float lastIn = 0.0f;
};

class TunePanel : public Panel
{
public:
    explicit TunePanel (UiContext&);
    void layout (juce::Rectangle<int>) override;
    void paint (juce::Graphics&) override;
    void tick() override;

private:
    void applyStyle (float retune, float humanize, float amount, const juce::String& name);

    PitchDisplay display;
    ChoiceCombo key, scale;
    ChoiceButtons source;
    juce::TextButton subtle { "SUBTLE" }, modern { "MODERN" }, hard { "HARD" };
    Knob retune, amount, humanize, voice;
    juce::Rectangle<int> keyLabel, scaleLabel, sourceLabel, styleLabel;
};

class ChangerPanel : public Panel
{
public:
    explicit ChangerPanel (UiContext&);
    void layout (juce::Rectangle<int>) override;
    void paint (juce::Graphics&) override;
    void tick() override;

private:
    Knob amount, pitch, formant, width;
    juce::Rectangle<int> hintArea;
    int lastType = -1;
};

class VocoderPanel : public Panel
{
public:
    explicit VocoderPanel (UiContext&);
    void layout (juce::Rectangle<int>) override;
    void paint (juce::Graphics&) override;
    void tick() override;

private:
    ChoiceButtons carrier;
    ParamToggle autoButton;
    Knob bands, release, formant, sibilance, level;
    juce::Rectangle<int> lcd, carrierLabel;
    std::array<float, Vocoder::maxBands> shown {};
};

class ChoirPanel : public Panel
{
public:
    explicit ChoirPanel (UiContext&);
    void layout (juce::Rectangle<int>) override;

private:
    ChoiceButtons vowels;
    Knob vowel, size, breath, vibrato, attack, release, level;
};

class VocalPanel : public Panel
{
public:
    explicit VocalPanel (UiContext&);
    void layout (juce::Rectangle<int>) override;

private:
    ChoiceButtons vowels;
    Knob vowel, vibrato, breath, bright, glide, attack, release, level;
};

class ArpPanel : public Panel
{
public:
    explicit ArpPanel (UiContext&);
    void layout (juce::Rectangle<int>) override;
    void paint (juce::Graphics&) override;
    void tick() override;

private:
    ChoiceButtons mode, octaves;
    ChoiceCombo rate;
    ParamToggle latch;
    Knob gate;
    juce::Rectangle<int> modeLabel, octLabel, rateLabel, leds;
    int shownStep = -1;
};

class FxPanel : public Panel
{
public:
    explicit FxPanel (UiContext&);
    void layout (juce::Rectangle<int>) override;
    void paint (juce::Graphics&) override;
    void tick() override;

private:
    Knob chorusRate, chorusDepth, chorusMix;
    ChoiceCombo delayTime;
    Knob delayFeedback, delayMix;
    Knob reverbSize, reverbDamp, reverbMix;
    Knob master;
    LevelMeter meterL, meterR;
    std::array<juce::Rectangle<int>, 3> rowLabels;
    juce::Rectangle<int> timeLabel;
};

// The playable keyboard along the bottom (handy in the Standalone, or without a controller).
class KeyboardStrip : public juce::Component
{
public:
    explicit KeyboardStrip (UiContext&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::MidiKeyboardComponent keys;
};
