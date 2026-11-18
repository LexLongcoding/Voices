// Voices - Copyright (C) 2026 LexLongcoding
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
#include "Theme.h"
#include "../PluginProcessor.h"
#include <functional>
#include <memory>
#include <vector>

// Shared by every widget: the processor plus a callback into the header LCD.
struct UiContext
{
    explicit UiContext (VoicesAudioProcessor& p) : proc (p) {}

    VoicesAudioProcessor& proc;
    std::function<void (const juce::String&, juce::Colour)> setLcd;

    juce::RangedAudioParameter* param (const char* id) const { return proc.apvts.getParameter (id); }
    float value (const char* id) const { return proc.apvts.getRawParameterValue (id)->load(); }
    void showValue (const char* id) const;
};

juce::String groupTitleFor (const juce::String& paramId);
juce::String noteNameOf (int midiNote);   // "A3" (middle C = C3, as in Decu6000)

//==============================================================================
// Parameter knob with its label underneath (double-click / right-click to reset).
class Knob : public juce::Component
{
public:
    Knob (UiContext&, const char* paramId, juce::Colour accent, juce::String labelOverride = {});
    ~Knob() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    UiContext& ctx;
    const char* id;
    juce::String label;
    float def = 0.0f;
    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

//==============================================================================
// On/off button bound to a toggle parameter.
class ParamToggle : public juce::TextButton
{
public:
    ParamToggle (UiContext&, const char* paramId, juce::String buttonText, juce::Colour accent);

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};

// A row of radio buttons bound to a choice parameter (index = value).
// `values` lets buttons map to arbitrary parameter values (default 0, 1, 2...).
class ChoiceButtons : public juce::Component
{
public:
    ChoiceButtons (UiContext&, const char* paramId, juce::StringArray labels, juce::Colour accent,
                   std::vector<float> values = {});
    void resized() override;
    int gap = 4;

private:
    void update (float v);

    UiContext& ctx;
    const char* id;
    std::vector<float> values;
    juce::OwnedArray<juce::TextButton> buttons;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

// Drop-down list bound to a choice parameter.
class ChoiceCombo : public juce::ComboBox
{
public:
    ChoiceCombo (UiContext&, const char* paramId, juce::Colour accent);

private:
    UiContext& ctx;
    const char* id;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

//==============================================================================
class LevelMeter : public juce::Component
{
public:
    void setLevel (float linearPeak);
    void paint (juce::Graphics&) override;

private:
    float level = 0.0f;   // displayed (decaying) 0..1
};

//==============================================================================
// Base for every panel: violet card with a coloured top bar, title and optional
// power button in the title row.
class Panel : public juce::Component
{
public:
    enum class Style { orange, synth, pink };

    Panel (UiContext&, juce::String title, Style, const char* powerParam = nullptr);

    void paint (juce::Graphics&) override;
    void resized() override;           // places the power button, then calls layout()
    virtual void layout (juce::Rectangle<int> content) = 0;
    virtual void tick();               // ~30 Hz from the editor (overrides call Panel::tick())

    juce::Colour accent() const;
    bool isOn() const;

protected:
    UiContext& ctx;
    juce::String title;
    Style style;
    const char* power;
    std::unique_ptr<ParamToggle> powerButton;
    juce::String status;               // small text at the right of the title row
    bool lastOn = true;

    static constexpr int titleRow = 34;
};
