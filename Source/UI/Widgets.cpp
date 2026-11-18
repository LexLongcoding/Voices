// Voices - Copyright (C) 2026 LexLongcoding
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "Widgets.h"

using namespace Theme;

juce::String groupTitleFor (const juce::String& paramId)
{
    const auto prefix = paramId.upToFirstOccurrenceOf ("_", false, false);
    for (const auto& g : Params::groups)
        if (prefix == g.prefix)
            return g.title;
    return prefix.toUpperCase();
}

juce::String noteNameOf (int note)
{
    return juce::MidiMessage::getMidiNoteName (note, true, true, 3);
}

juce::String paramTitleOf (int paramIndex)
{
    const auto& s = Params::specs[juce::jlimit (0, Params::numParams - 1, paramIndex)];
    return groupTitleFor (s.id) + " " + s.knob;
}

void UiContext::showValue (const char* id) const
{
    if (setLcd == nullptr) return;
    if (auto* p = param (id))
    {
        const int i = VoicesAudioProcessor::paramIndexForId (id);
        setLcd (groupTitleFor (id) + " " + Params::specs[i].knob + "  " + p->getCurrentValueAsText(), cyan);
    }
}

//==============================================================================
Knob::Knob (UiContext& c, const char* paramId, juce::Colour accent, juce::String labelOverride)
    : LearnableControl (paramId), ctx (c), id (paramId)
{
    const auto& spec = Params::specs[VoicesAudioProcessor::paramIndexForId (paramId)];
    label = labelOverride.isNotEmpty() ? labelOverride : juce::String (spec.knob);
    def = spec.def;

    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    slider.setTooltip (spec.name);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (ctx.proc.apvts, id, slider);
    slider.setDoubleClickReturnValue (true, def);
    slider.onDragStart   = [this] { ctx.showValue (id); };
    slider.onValueChange = [this] { if (slider.isMouseButtonDown() || slider.hasKeyboardFocus (false)) ctx.showValue (id); };
    slider.addMouseListener (this, false);
    addAndMakeVisible (slider);
}

Knob::~Knob() { slider.removeMouseListener (this); }

void Knob::mouseDown (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu())
        return;
    const int param = getLearnParam();
    const int cc = ctx.proc.ccForParam (param);
    juce::PopupMenu m;
    m.addSectionHeader (Params::specs[param].name);
    m.addItem (1, "Learn MIDI CC...");
    m.addItem (2, cc >= 0 ? "Clear MIDI CC (CC " + juce::String (cc) + ")" : juce::String ("Clear MIDI CC"), cc >= 0);
    m.addItem (3, "Reset to default");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&slider),
                     [safe = juce::Component::SafePointer<Knob> (this)] (int r)
                     {
                         if (safe == nullptr) return;
                         auto& c = safe->ctx;
                         const int p = safe->getLearnParam();
                         if (r == 1 && c.armLearn) c.armLearn (p);
                         if (r == 2)
                         {
                             c.proc.clearCcForParam (p);
                             if (c.setLcd) c.setLcd (paramTitleOf (p) + "  //  MIDI CC CLEARED", Theme::learn);
                         }
                         if (r == 3) safe->slider.setValue (safe->def, juce::sendNotificationSync);
                     });
}

namespace { int knobLabelHeight (int h) { return h > 100 ? 18 : 14; } }

void Knob::resized()
{
    auto r = getLocalBounds();
    r.removeFromBottom (knobLabelHeight (getHeight()));
    const int size = juce::jmin (r.getWidth(), r.getHeight());
    slider.setBounds (r.withSizeKeepingCentre (size, size));
}

void Knob::paint (juce::Graphics& g)
{
    g.setColour (text);
    const int lh = knobLabelHeight (getHeight());
    g.setFont (mono (lh > 14 ? 13.0f : 10.0f));
    g.drawFittedText (label, getLocalBounds().removeFromBottom (lh), juce::Justification::centred, 1, 0.55f);
}

//==============================================================================
ParamToggle::ParamToggle (UiContext& ctx, const char* paramId, juce::String buttonText, juce::Colour accent)
    : juce::TextButton (std::move (buttonText)), LearnableControl (paramId)
{
    setClickingTogglesState (true);
    setColour (juce::TextButton::buttonOnColourId, accent);
    setTooltip (Params::specs[VoicesAudioProcessor::paramIndexForId (paramId)].name);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (ctx.proc.apvts, paramId, *this);
    onClick = [&ctx, paramId] { ctx.showValue (paramId); };
}

//==============================================================================
ChoiceButtons::ChoiceButtons (UiContext& c, const char* paramId, juce::StringArray labels, juce::Colour accent,
                              std::vector<float> vals)
    : LearnableControl (paramId), ctx (c), id (paramId), values (std::move (vals))
{
    if (values.empty())
        for (int i = 0; i < labels.size(); ++i) values.push_back (static_cast<float> (i));

    auto* p = ctx.param (id);
    attachment = std::make_unique<juce::ParameterAttachment> (*p, [this] (float v) { update (v); });

    for (int i = 0; i < labels.size(); ++i)
    {
        auto* b = buttons.add (new juce::TextButton (labels[i]));
        b->setColour (juce::TextButton::buttonOnColourId, accent);
        b->setTooltip (p->getName (64));
        b->onClick = [this, i]
        {
            attachment->setValueAsCompleteGesture (values[static_cast<size_t> (i)]);
            ctx.showValue (id);
        };
        addAndMakeVisible (b);
    }
    attachment->sendInitialUpdate();
}

void ChoiceButtons::update (float v)
{
    // highlight the button whose value is nearest (continuous params, e.g. the vowel knob)
    int best = 0;
    for (int i = 1; i < buttons.size(); ++i)
        if (std::abs (values[static_cast<size_t> (i)] - v) < std::abs (values[static_cast<size_t> (best)] - v)) best = i;
    const bool exact = std::abs (values[static_cast<size_t> (best)] - v) < 0.12f;
    for (int i = 0; i < buttons.size(); ++i)
        buttons[i]->setToggleState (exact && i == best, juce::dontSendNotification);
}

void ChoiceButtons::resized()
{
    auto r = getLocalBounds();
    const int n = buttons.size();
    if (n == 0) return;
    const int w = (r.getWidth() - gap * (n - 1)) / n;
    for (int i = 0; i < n; ++i)
    {
        buttons[i]->setBounds (r.removeFromLeft (i == n - 1 ? r.getWidth() : w));
        r.removeFromLeft (gap);
    }
}

//==============================================================================
ChoiceCombo::ChoiceCombo (UiContext& c, const char* paramId, juce::Colour accent)
    : LearnableControl (paramId), ctx (c), id (paramId)
{
    const auto& spec = Params::specs[VoicesAudioProcessor::paramIndexForId (id)];
    int count = 0;
    const auto* labels = Params::choiceLabels (spec.choices, count);
    for (int i = 0; i < count; ++i)
        addItem (labels[i], i + 1);
    setColour (juce::ComboBox::outlineColourId, accent);
    setColour (juce::ComboBox::arrowColourId, accent);
    setTooltip (spec.name);

    attachment = std::make_unique<juce::ParameterAttachment> (*ctx.param (id), [this] (float v)
    {
        setSelectedId (juce::roundToInt (v) + 1, juce::dontSendNotification);
    });
    onChange = [this]
    {
        if (getSelectedId() > 0)
        {
            attachment->setValueAsCompleteGesture (static_cast<float> (getSelectedId() - 1));
            ctx.showValue (id);
        }
    };
    attachment->sendInitialUpdate();
}

//==============================================================================
void LevelMeter::setLevel (float peak)
{
    const float db = juce::Decibels::gainToDecibels (peak, -60.0f);
    const float target = juce::jlimit (0.0f, 1.0f, (db + 48.0f) / 48.0f);
    const float next = juce::jmax (target, level * 0.86f);
    if (std::abs (next - level) > 0.002f) { level = next; repaint(); }
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    juce::ColourGradient grad (cyan, 0.0f, r.getBottom(), pink, 0.0f, r.getY(), false);
    grad.addColour (0.75, orange);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (r, 2.0f);
    g.setColour (bg);
    g.fillRect (r.withHeight (r.getHeight() * (1.0f - level)));
}

//==============================================================================
Panel::Panel (UiContext& c, juce::String t, Style s, const char* powerParam)
    : ctx (c), title (std::move (t)), style (s), power (powerParam)
{
    if (power != nullptr)
    {
        powerButton = std::make_unique<ParamToggle> (ctx, power, "ON", style == Style::orange ? orange : cyan);
        addAndMakeVisible (*powerButton);
    }
}

juce::Colour Panel::accent() const
{
    switch (style)
    {
        case Style::orange: return orange;
        case Style::synth:  return cyan;
        case Style::pink:   return pink;
    }
    return orange;
}

bool Panel::isOn() const { return power == nullptr || ctx.value (power) >= 0.5f; }

void Panel::tick()
{
    if (isOn() != lastOn) { lastOn = isOn(); repaint(); }
}

void Panel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    if (style == Style::synth)
    {
        g.setGradientFill (juce::ColourGradient (panel, 0.0f, 0.0f, panelLo, 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle (r, 4.0f);
        g.setGradientFill (juce::ColourGradient (cyan, 0.0f, 0.0f, pink, r.getRight(), 0.0f, false));
    }
    else
    {
        g.setColour (panel);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (accent());
    }
    juce::Path topBar;
    topBar.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), 4.0f, 4.0f, 4.0f, true, true, false, false);
    g.fillPath (topBar);

    auto head = getLocalBounds().reduced (14, 0).withTrimmedTop (10).withHeight (20);
    g.setColour (isOn() ? text : dim);
    g.setFont (display (13.0f));
    g.drawText (title, head, juce::Justification::centredLeft, false);

    if (status.isNotEmpty())
    {
        auto st = head;
        if (powerButton != nullptr) st.removeFromRight (powerButton->getWidth() + 10);
        g.setColour (isOn() ? accent() : dim);
        g.setFont (mono (11.0f));
        g.drawText (status, st, juce::Justification::centredRight, false);
    }
}

void Panel::resized()
{
    if (powerButton != nullptr)
        powerButton->setBounds (getWidth() - 14 - 46, 9, 46, 22);
    layout (getLocalBounds().reduced (14, 0).withTrimmedTop (titleRow + 8).withTrimmedBottom (12));
}
