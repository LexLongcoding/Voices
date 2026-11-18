// Voices - Copyright (C) 2026 LexLongcoding
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "UI/Theme.h"
#include "UI/Panels.h"

// LCD in the header: shows the current patch ("PATCH 02 // CATHEDRAL AAHS *"),
// temporarily replaced by knob values. Click for the patch menu.
class LcdDisplay : public juce::Component
{
public:
    void setPatchText (const juce::String& t);
    void showMessage (const juce::String& t, juce::Colour c, int holdMs = 2500);
    void clearMessage();
    void tick();
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }

    std::function<void()> onClick;

private:
    juce::String patchText, message;
    juce::Colour messageColour { Theme::cyan };
    juce::uint32 messageUntil = 0;
    bool showingMessage() const;
};

// Header box: is a voice coming in, and how loud?
class InputBadge : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit InputBadge (VoicesAudioProcessor& p);
    void paint (juce::Graphics&) override;
    void tick();

private:
    VoicesAudioProcessor& proc;
    float level = 0.0f;
    bool hasInput = false;
};

//==============================================================================
class VoicesAudioProcessorEditor : public juce::AudioProcessorEditor,
                                   private juce::Timer
{
public:
    explicit VoicesAudioProcessorEditor (VoicesAudioProcessor&);
    ~VoicesAudioProcessorEditor() override;

    void resized() override;

private:
    // Everything is laid out on a fixed 1440x900 design canvas, scaled to the window.
    class Canvas : public juce::Component
    {
    public:
        explicit Canvas (bool fx) : isFx (fx) {}
        void paint (juce::Graphics&) override;
    private:
        juce::Image background;
        bool isFx;
    };

    void timerCallback() override;
    void setLcd (const juce::String& t, juce::Colour c = Theme::cyan) { lcd.showMessage (t, c); }
    void showPatchMenu();
    void saveAsDialog();
    void saveTo (const juce::String& name);
    void confirmDelete();
    void updatePatchText();

    VoicesAudioProcessor& voices;
    Theme::FontKeeper fonts;
    Theme::LookAndFeel lnf;
    UiContext ctx;

    Canvas canvas;
    ChoiceButtons voiceType;
    LcdDisplay lcd;
    juce::ArrowButton prevPatch { "prev", 0.5f, Theme::orange }, nextPatch { "next", 0.0f, Theme::orange };
    InputBadge inputBadge;

    TunePanel tune;
    ChangerPanel changer;
    VocoderPanel vocoder;
    ChoirPanel choir;
    VocalPanel vocal;
    ArpPanel arp;
    FxPanel fx;
    KeyboardStrip keyboard;
    std::array<Panel*, 7> panels;

    juce::TooltipWindow tooltips { this, 700 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoicesAudioProcessorEditor)
};
