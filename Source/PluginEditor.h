// Voices - Copyright (C) 2026 LexLongcoding
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "UI/Theme.h"
#include "UI/Panels.h"

// LCD in the header: shows the current patch ("PATCH 02 // CATHEDRAL AAHS *"),
// temporarily replaced by messages (knob values, MIDI learn). Click for the patch menu.
class LcdDisplay : public juce::Component
{
public:
    void setPatchText (const juce::String& t);
    void showMessage (const juce::String& t, juce::Colour c, int holdMs = 2500);   // holdMs < 0: until replaced
    void clearMessage();
    void tick();
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }

    std::function<void()> onClick;

private:
    juce::String patchText, message;
    juce::Colour messageColour { Theme::cyan };
    juce::uint32 messageUntil = 0;
    bool persistent = false;
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

// Header button that turns MIDI LEARN mode on and off; right-click for its menu.
class LearnButton : public juce::TextButton
{
public:
    LearnButton() : juce::TextButton ("MIDI LEARN") {}
    std::function<void()> onRightClick;
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick(); return; }
        juce::TextButton::mouseDown (e);
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu()) juce::TextButton::mouseUp (e);
    }
};

// Shown over the whole editor in MIDI LEARN mode. Outlines every control that can
// be learned with its current CC; click one to arm it, right-click to clear it.
class LearnOverlay : public juce::Component
{
public:
    explicit LearnOverlay (UiContext&);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    std::function<void (int paramIndex)> onArmed;   // for LCD messages
    std::function<void (int paramIndex)> onCleared;
    std::function<void()> onCancelled;

private:
    struct Target { int param; juce::Rectangle<int> bounds; };
    std::vector<Target> targets();

    UiContext& ctx;
};

//==============================================================================
class VoicesAudioProcessorEditor : public juce::AudioProcessorEditor,
                                   private juce::Timer
{
public:
    explicit VoicesAudioProcessorEditor (VoicesAudioProcessor&);
    ~VoicesAudioProcessorEditor() override;

    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

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
    void setLcd (const juce::String& t, juce::Colour c = Theme::cyan);
    void setLearnMode (bool on);
    void armLearn (int paramIndex);
    void showLearnMenu();
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
    LearnButton learnButton;

    TunePanel tune;
    ChangerPanel changer;
    VocoderPanel vocoder;
    ChoirPanel choir;
    VocalPanel vocal;
    ArpPanel arp;
    FxPanel fx;
    KeyboardStrip keyboard;
    std::array<Panel*, 7> panels;
    LearnOverlay overlay;
    int seenCcLearnCount = 0, tickCount = 0;

    juce::TooltipWindow tooltips { this, 700 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoicesAudioProcessorEditor)
};
