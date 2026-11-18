// Voices - Copyright (C) 2026 LexLongcoding
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "Panels.h"

using namespace Theme;

namespace
{
    void drawLabel (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& t, juce::Colour c = dim)
    {
        g.setColour (c);
        g.setFont (mono (10.0f));
        g.drawText (t, r, juce::Justification::centredLeft, false);
    }

    void drawLcd (juce::Graphics& g, juce::Rectangle<int> r, juce::Colour edge)
    {
        g.setColour (lcdBg);
        g.fillRoundedRectangle (r.toFloat(), 3.0f);
        g.setColour (edge.withAlpha (0.6f));
        g.drawRoundedRectangle (r.toFloat(), 3.0f, 1.0f);
    }

    // Lay knobs out left to right in equal cells.
    void row (juce::Rectangle<int> r, std::initializer_list<juce::Component*> comps)
    {
        const int w = r.getWidth() / static_cast<int> (comps.size());
        for (auto* c : comps)
            c->setBounds (r.removeFromLeft (w));
    }

    uint16_t displayMask (UiContext& ctx)
    {
        const int scale = juce::jlimit (0, 9, juce::roundToInt (ctx.value ("tune_scale")));
        const int key = juce::roundToInt (ctx.value ("tune_key"));
        return Scales::inKey (Scales::all[static_cast<size_t> (scale)].mask, key);
    }
}

//==============================================================================
void PitchDisplay::push()
{
    auto& r = ctx.proc.readout;
    const bool v = r.voiced.load();
    const float i = v ? r.inputMidi.load() : 0.0f;
    const float o = v ? r.outputMidi.load() : 0.0f;
    in[static_cast<size_t> (head)] = i;
    out[static_cast<size_t> (head)] = o;
    head = (head + 1) % historySize;
    if (v)
    {
        centre += (o - centre) * 0.08f;   // the view follows the singer
        lastIn = i;
    }
    lastTarget = v ? r.targetNote.load() : -1;
    repaint();
}

void PitchDisplay::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    drawLcd (g, r, orange);
    auto inner = r.reduced (8, 6);
    const bool on = ctx.value ("tune_on") >= 0.5f;
    const bool hasInput = ctx.proc.readout.hasInput.load();

    // header line
    auto top = inner.removeFromTop (16);
    g.setFont (mono (10.0f));
    g.setColour (text.withAlpha (0.75f));
    g.drawText (! hasInput ? "NO VOICE INPUT // SEE MANUAL" : (on ? "PITCH // SUNG vs TUNED" : "PITCH // AUTO-TUNE OFF"),
                top, juce::Justification::centredLeft, false);
    if (lastTarget >= 0)
    {
        const float cents = (lastIn - static_cast<float> (lastTarget)) * 100.0f;
        g.setColour (pink);
        g.setFont (display (16.0f, true));
        g.drawText (noteNameOf (lastTarget), top.withTrimmedRight (52).translated (0, 2), juce::Justification::centredRight, false);
        g.setFont (mono (11.0f));
        g.setColour (std::abs (cents) < 10.0f ? cyan : orange);
        g.drawText ((cents >= 0 ? "+" : "") + juce::String (juce::roundToInt (cents)) + " c", top, juce::Justification::centredRight, false);
    }

    // note grid: +-7 semitones around the singer, scale notes brighter
    const auto mask = displayMask (ctx);
    const float span = 14.0f;
    const float lo = centre - span * 0.5f;
    const float y0 = static_cast<float> (inner.getBottom()), h = static_cast<float> (inner.getHeight());
    auto yFor = [&] (float midi) { return y0 - (midi - lo) / span * h; };
    g.setFont (mono (9.0f));
    for (int n = static_cast<int> (std::ceil (lo)); n <= static_cast<int> (lo + span); ++n)
    {
        const bool inScale = (mask >> (((n % 12) + 12) % 12)) & 1;
        const float y = yFor (static_cast<float> (n));
        const bool isTarget = n == lastTarget;
        g.setColour (isTarget ? pink.withAlpha (0.45f) : (inScale ? text.withAlpha (0.16f) : text.withAlpha (0.05f)));
        g.drawHorizontalLine (juce::roundToInt (y), static_cast<float> (inner.getX() + 26), static_cast<float> (inner.getRight()));
        if (inScale)
        {
            g.setColour (isTarget ? pink : dim.withAlpha (0.8f));
            g.drawText (noteNameOf (n), inner.getX(), juce::roundToInt (y) - 6, 26, 12, juce::Justification::centredLeft, false);
        }
    }

    // traces (oldest -> newest, left -> right)
    auto trace = [&] (const std::array<float, historySize>& data, juce::Colour c, float thickness)
    {
        juce::Path p;
        bool drawing = false;
        const float x0 = static_cast<float> (inner.getX() + 28), w = static_cast<float> (inner.getWidth() - 28);
        for (int k = 0; k < historySize; ++k)
        {
            const float v = data[static_cast<size_t> ((head + k) % historySize)];
            if (v <= 0.0f) { drawing = false; continue; }
            const float x = x0 + w * static_cast<float> (k) / static_cast<float> (historySize - 1);
            const float y = juce::jlimit (static_cast<float> (inner.getY()), y0, yFor (v));
            if (! drawing) { p.startNewSubPath (x, y); drawing = true; }
            else p.lineTo (x, y);
        }
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };
    trace (in, cyan.withAlpha (on ? 0.55f : 0.9f), 1.5f);
    if (on) trace (out, pink, 2.2f);
}

//==============================================================================
TunePanel::TunePanel (UiContext& c)
    : Panel (c, "AUTO-TUNE", Style::orange, "tune_on"),
      display (c),
      key (c, "tune_key", orange), scale (c, "tune_scale", orange),
      source (c, "tune_source", { "SCALE", "MIDI" }, orange),
      retune (c, "tune_retune", orange), amount (c, "tune_amount", orange),
      humanize (c, "tune_humanize", orange), voice (c, "voice_level", orange)
{
    for (juce::Component* comp : std::initializer_list<juce::Component*> { &display, &key, &scale, &source, &subtle, &modern, &hard,
                                                                           &retune, &amount, &humanize, &voice })
        addAndMakeVisible (comp);

    subtle.setTooltip ("Gentle, transparent correction (slow retune, humanize up)");
    modern.setTooltip ("Polished modern pop tuning");
    hard.setTooltip ("Instant retune: the stepped, robotic hip-hop effect");
    subtle.onClick = [this] { applyStyle (0.15f, 0.7f, 0.8f, "SUBTLE"); };
    modern.onClick = [this] { applyStyle (0.04f, 0.3f, 1.0f, "MODERN"); };
    hard.onClick   = [this] { applyStyle (0.0f, 0.0f, 1.0f, "HARD"); };
}

void TunePanel::applyStyle (float rt, float hz, float am, const juce::String& name)
{
    for (auto [id, v] : { std::pair<const char*, float> { "tune_retune", rt }, { "tune_humanize", hz }, { "tune_amount", am }, { "tune_on", 1.0f } })
        if (auto* p = ctx.param (id))
            p->setValueNotifyingHost (p->convertTo0to1 (v));
    if (ctx.setLcd) ctx.setLcd ("AUTO-TUNE STYLE  " + name, orange);
}

void TunePanel::layout (juce::Rectangle<int> r)
{
    auto knobs = r.removeFromBottom (88);
    row (knobs, { &retune, &amount, &humanize, &voice });
    r.removeFromBottom (8);

    display.setBounds (r.removeFromLeft (320));
    r.removeFromLeft (16);

    auto line = [&] (juce::Rectangle<int>& label, int h)
    {
        label = r.removeFromTop (14);
        auto c = r.removeFromTop (h);
        r.removeFromTop (8);
        return c;
    };
    auto keyRow = line (keyLabel, 26);
    scaleLabel = keyLabel.withTrimmedLeft (82);
    keyLabel = keyLabel.withWidth (72);
    key.setBounds (keyRow.removeFromLeft (72));
    keyRow.removeFromLeft (10);
    scale.setBounds (keyRow);
    source.setBounds (line (sourceLabel, 26));
    auto st = line (styleLabel, 26);
    const int w = (st.getWidth() - 8) / 3;
    subtle.setBounds (st.removeFromLeft (w)); st.removeFromLeft (4);
    modern.setBounds (st.removeFromLeft (w)); st.removeFromLeft (4);
    hard.setBounds (st);
}

void TunePanel::paint (juce::Graphics& g)
{
    Panel::paint (g);
    drawLabel (g, keyLabel, "KEY");
    drawLabel (g, scaleLabel, "SCALE");
    drawLabel (g, sourceLabel, "NOTES FROM");
    drawLabel (g, styleLabel, "STYLE // NATURAL -> ROBOT");
}

void TunePanel::tick()
{
    Panel::tick();
    display.push();
}

//==============================================================================
ChangerPanel::ChangerPanel (UiContext& c)
    : Panel (c, "VOICE CHANGER", Style::orange, "chg_on"),
      amount (c, "chg_amount", orange), pitch (c, "chg_pitch", orange),
      formant (c, "chg_formant", orange), width (c, "chg_width", orange)
{
    for (auto* k : { &amount, &pitch, &formant, &width })
        addAndMakeVisible (k);
}

void ChangerPanel::layout (juce::Rectangle<int> r)
{
    hintArea = r.removeFromTop (30);
    auto bottom = r.removeFromBottom (84);
    row (bottom, { &pitch, &formant, &width });
    r.removeFromBottom (4);
    amount.setBounds (r.withSizeKeepingCentre (juce::jmin (r.getWidth(), 150), r.getHeight()));
}

void ChangerPanel::paint (juce::Graphics& g)
{
    Panel::paint (g);
    const int type = juce::roundToInt (ctx.value ("master_voicetype"));
    static const char* targets[] = { "DEEPER, MALE", "HIGHER, FEMALE", "MALE + FEMALE DOUBLE" };
    g.setFont (mono (10.0f));
    g.setColour (text.withAlpha (0.75f));
    g.drawFittedText (juce::String ("TARGET: ") + targets[juce::jlimit (0, 2, type)] + "\nPICK THE VOICE TYPE UP TOP, TURN AMOUNT",
                      hintArea, juce::Justification::centredLeft, 2);
}

void ChangerPanel::tick()
{
    Panel::tick();
    const int type = juce::roundToInt (ctx.value ("master_voicetype"));
    if (type != lastType)
    {
        lastType = type;
        static const char* names[] = { "> MALE", "> FEMALE", "> MIXED" };
        status = names[juce::jlimit (0, 2, type)];
        width.setEnabled (type == 2);
        repaint();
    }
}

//==============================================================================
VocoderPanel::VocoderPanel (UiContext& c)
    : Panel (c, "VOCODER", Style::orange, "voc_on"),
      carrier (c, "voc_carrier", { "SAW", "PULSE", "SUPER" }, orange),
      autoButton (c, "voc_auto", "AUTO", cyan),
      bands (c, "voc_bands", orange), release (c, "voc_release", orange), formant (c, "voc_formant", orange),
      sibilance (c, "voc_sibilance", orange, "SIBIL"), level (c, "voc_level", orange)
{
    for (juce::Component* comp : std::initializer_list<juce::Component*> { &carrier, &autoButton, &bands, &release, &formant, &sibilance, &level })
        addAndMakeVisible (comp);
    autoButton.setTooltip ("AUTO: with no keys held the vocoder sings the pitch you sing (robot voice, no keyboard needed)");
}

void VocoderPanel::layout (juce::Rectangle<int> r)
{
    auto knobs = r.removeFromBottom (88);
    row (knobs, { &bands, &release, &formant, &sibilance, &level });
    r.removeFromBottom (8);
    auto ctrl = r.removeFromBottom (26);
    carrierLabel = r.removeFromBottom (14);
    r.removeFromBottom (6);
    lcd = r;
    autoButton.setBounds (ctrl.removeFromRight (80));
    ctrl.removeFromRight (12);
    carrier.setBounds (ctrl);
}

void VocoderPanel::paint (juce::Graphics& g)
{
    Panel::paint (g);
    drawLabel (g, carrierLabel, "CARRIER");
    drawLabel (g, carrierLabel.withTrimmedLeft (carrierLabel.getWidth() - 80), "FOLLOW VOICE");
    drawLcd (g, lcd, orange);

    auto inner = lcd.reduced (8, 6);
    g.setFont (mono (10.0f));
    g.setColour (text.withAlpha (0.75f));
    const bool on = isOn();
    g.drawText (on ? "BANDS // YOUR VOICE SHAPES THE SYNTH" : "VOCODER OFF", inner.removeFromTop (14), juce::Justification::centredLeft, false);
    inner.removeFromTop (4);

    const int n = juce::jlimit (8, Vocoder::maxBands, ctx.proc.readout.numBands.load());
    const float w = static_cast<float> (inner.getWidth()) / static_cast<float> (n);
    for (int b = 0; b < n; ++b)
    {
        const float lvl = on ? shown[static_cast<size_t> (b)] : 0.0f;
        const float x = static_cast<float> (inner.getX()) + w * static_cast<float> (b);
        auto bar = juce::Rectangle<float> (x + 1.0f, static_cast<float> (inner.getY()), w - 2.0f, static_cast<float> (inner.getHeight()));
        g.setColour (orange.withAlpha (0.08f));
        g.fillRect (bar);
        const float hgt = bar.getHeight() * lvl;
        juce::ColourGradient grad (orange, 0.0f, bar.getBottom(), pink, 0.0f, bar.getY(), false);
        g.setGradientFill (grad);
        g.fillRect (bar.withTop (bar.getBottom() - hgt));
    }
}

void VocoderPanel::tick()
{
    Panel::tick();
    const int n = juce::jlimit (8, Vocoder::maxBands, ctx.proc.readout.numBands.load());
    bool changed = false;
    for (int b = 0; b < n; ++b)
    {
        const float lvl = ctx.proc.readout.bands[static_cast<size_t> (b)].load();
        const float db = juce::Decibels::gainToDecibels (lvl, -60.0f);
        const float target = juce::jlimit (0.0f, 1.0f, (db + 54.0f) / 48.0f);
        auto& s = shown[static_cast<size_t> (b)];
        const float next = juce::jmax (target, s * 0.82f);
        if (std::abs (next - s) > 0.003f) { s = next; changed = true; }
    }
    if (changed) repaint (lcd);
}

//==============================================================================
ChoirPanel::ChoirPanel (UiContext& c)
    : Panel (c, "CHOIR", Style::synth, "choir_on"),
      vowels (c, "choir_vowel", { "OOH", "OH", "AAH" }, cyan, { 0.0f, 0.5f, 1.0f }),
      vowel (c, "choir_vowel", cyan, "OOH/AAH"), size (c, "choir_size", cyan), breath (c, "choir_breath", cyan),
      vibrato (c, "choir_vibrato", cyan), attack (c, "choir_attack", cyan), release (c, "choir_release", cyan),
      level (c, "choir_level", pink)
{
    for (juce::Component* comp : std::initializer_list<juce::Component*> { &vowels, &vowel, &size, &breath, &vibrato, &attack, &release, &level })
        addAndMakeVisible (comp);
}

void ChoirPanel::layout (juce::Rectangle<int> r)
{
    vowels.setBounds (r.removeFromTop (26));
    r.removeFromTop (8);
    const int h = r.getHeight() / 2;
    row (r.removeFromTop (h), { &vowel, &size, &breath, &vibrato });
    auto second = r;
    const int cw = second.getWidth() / 4;
    row (second.withTrimmedRight (cw), { &attack, &release, &level });
}

//==============================================================================
VocalPanel::VocalPanel (UiContext& c)
    : Panel (c, "VOCAL SYNTH", Style::synth, "vsyn_on"),
      vowels (c, "vsyn_vowel", { "A", "E", "I", "O", "U" }, cyan),
      vowel (c, "vsyn_vowel", cyan), vibrato (c, "vsyn_vibrato", cyan), breath (c, "vsyn_breath", cyan),
      bright (c, "vsyn_bright", cyan), glide (c, "vsyn_glide", cyan), attack (c, "vsyn_attack", cyan),
      release (c, "vsyn_release", cyan), level (c, "vsyn_level", pink)
{
    for (juce::Component* comp : std::initializer_list<juce::Component*> { &vowels, &vowel, &vibrato, &breath, &bright, &glide, &attack, &release, &level })
        addAndMakeVisible (comp);
}

void VocalPanel::layout (juce::Rectangle<int> r)
{
    vowels.setBounds (r.removeFromTop (26));
    r.removeFromTop (8);
    const int h = r.getHeight() / 2;
    row (r.removeFromTop (h), { &vowel, &vibrato, &breath, &bright });
    row (r, { &glide, &attack, &release, &level });
}

//==============================================================================
ArpPanel::ArpPanel (UiContext& c)
    : Panel (c, "ARPEGGIATOR", Style::synth, "arp_on"),
      mode (c, "arp_mode", { "TREM", "UP", "DOWN", "RAND" }, cyan),
      octaves (c, "arp_octaves", { "1", "2", "3", "4" }, cyan, { 1.0f, 2.0f, 3.0f, 4.0f }),
      rate (c, "arp_rate", cyan),
      latch (c, "arp_latch", "LATCH", pink),
      gate (c, "arp_gate", cyan)
{
    for (juce::Component* comp : std::initializer_list<juce::Component*> { &mode, &octaves, &rate, &latch, &gate })
        addAndMakeVisible (comp);
    latch.setTooltip ("LATCH: the arp keeps playing after you let go; play a new chord to replace it");
}

void ArpPanel::layout (juce::Rectangle<int> r)
{
    modeLabel = r.removeFromTop (14);
    mode.setBounds (r.removeFromTop (24));
    r.removeFromTop (6);
    octLabel = r.removeFromTop (14);
    octaves.setBounds (r.removeFromTop (24));
    r.removeFromTop (6);
    rateLabel = r.removeFromTop (14);
    auto rr = r.removeFromTop (26);
    latch.setBounds (rr.removeFromRight (78));
    rr.removeFromRight (8);
    rate.setBounds (rr);
    r.removeFromTop (6);
    gate.setBounds (r.removeFromLeft (76));
    r.removeFromLeft (10);
    leds = r.withSizeKeepingCentre (r.getWidth(), 22).translated (0, -6);
}

void ArpPanel::paint (juce::Graphics& g)
{
    Panel::paint (g);
    drawLabel (g, modeLabel, "MODE");
    drawLabel (g, octLabel, "OCTAVES");
    drawLabel (g, rateLabel, "RATE // SYNCED TO TEMPO");

    // 8 step LEDs
    const int n = 8;
    const float w = static_cast<float> (leds.getWidth()) / n;
    for (int i = 0; i < n; ++i)
    {
        auto led = juce::Rectangle<float> (static_cast<float> (leds.getX()) + w * static_cast<float> (i) + 2.0f,
                                           static_cast<float> (leds.getY()), w - 4.0f, static_cast<float> (leds.getHeight()));
        const bool lit = isOn() && shownStep >= 0 && (shownStep % n) == i;
        if (lit)
        {
            g.setColour (pink.withAlpha (0.3f));
            g.fillRoundedRectangle (led.expanded (2.0f), 3.0f);
        }
        g.setColour (lit ? pink : lcdBg);
        g.fillRoundedRectangle (led, 2.0f);
        g.setColour (cyan.withAlpha (0.35f));
        g.drawRoundedRectangle (led, 2.0f, 1.0f);
    }
    g.setColour (dim);
    g.setFont (mono (10.0f));
    g.drawText ("STEP", leds.translated (0, 24).withHeight (12), juce::Justification::centredLeft, false);
}

void ArpPanel::tick()
{
    Panel::tick();
    const int s = ctx.proc.readout.arpStep.load();
    if (s != shownStep) { shownStep = s; repaint (leds.expanded (4)); }
}

//==============================================================================
FxPanel::FxPanel (UiContext& c)
    : Panel (c, "FX // MASTER", Style::pink),
      chorusRate (c, "chorus_rate", pink), chorusDepth (c, "chorus_depth", pink), chorusMix (c, "chorus_mix", pink),
      delayTime (c, "delay_time", pink),
      delayFeedback (c, "delay_feedback", pink), delayMix (c, "delay_mix", pink),
      reverbSize (c, "reverb_size", pink), reverbDamp (c, "reverb_damp", pink), reverbMix (c, "reverb_mix", pink),
      master (c, "master_level", pink)
{
    for (juce::Component* comp : std::initializer_list<juce::Component*> { &chorusRate, &chorusDepth, &chorusMix, &delayTime,
                                                                           &delayFeedback, &delayMix, &reverbSize, &reverbDamp,
                                                                           &reverbMix, &master, &meterL, &meterR })
        addAndMakeVisible (comp);
}

void FxPanel::layout (juce::Rectangle<int> r)
{
    auto right = r.removeFromRight (96);
    r.removeFromRight (10);
    auto meters = right.removeFromRight (26);
    auto mh = meters.withSizeKeepingCentre (26, juce::jmin (150, meters.getHeight()));
    meterL.setBounds (mh.removeFromLeft (11));
    meterR.setBounds (mh.removeFromRight (11));
    right.removeFromRight (4);
    master.setBounds (right.withSizeKeepingCentre (right.getWidth(), juce::jmin (right.getHeight(), 100)));

    const int h = r.getHeight() / 3;
    for (int i = 0; i < 3; ++i)
    {
        auto line = r.removeFromTop (h);
        rowLabels[static_cast<size_t> (i)] = line.removeFromLeft (56);
        if (i == 0) row (line, { &chorusRate, &chorusDepth, &chorusMix });
        if (i == 1)
        {
            const int cw = line.getWidth() / 3;
            auto timeCell = line.removeFromLeft (cw).reduced (1, 0);
            timeLabel = timeCell.withTrimmedTop (timeCell.getHeight() - 14);
            delayTime.setBounds (timeCell.withSizeKeepingCentre (timeCell.getWidth(), 26).translated (0, -6));
            row (line, { &delayFeedback, &delayMix });
        }
        if (i == 2) row (line, { &reverbSize, &reverbDamp, &reverbMix });
    }
}

void FxPanel::paint (juce::Graphics& g)
{
    Panel::paint (g);
    const char* names[] = { "CHORUS", "DELAY", "REVERB" };
    for (size_t i = 0; i < 3; ++i)
    {
        g.setColour (text);
        g.setFont (display (10.0f));
        g.drawText (names[i], rowLabels[i], juce::Justification::centredLeft, false);
    }
    g.setColour (text);
    g.setFont (mono (10.0f));
    g.drawText ("TIME", timeLabel, juce::Justification::centred, false);
}

void FxPanel::tick()
{
    Panel::tick();
    meterL.setLevel (ctx.proc.readout.out[0].load());
    meterR.setLevel (ctx.proc.readout.out[1].load());
}

//==============================================================================
KeyboardStrip::KeyboardStrip (UiContext& c)
    : keys (c.proc.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    keys.setAvailableRange (36, 96);
    keys.setScrollButtonsVisible (false);
    keys.setVelocity (0.8f, true);
    keys.setColour (juce::MidiKeyboardComponent::whiteNoteColourId, text);
    keys.setColour (juce::MidiKeyboardComponent::blackNoteColourId, panelLo);
    keys.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, bg);
    keys.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, cyan.withAlpha (0.35f));
    keys.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, pink.withAlpha (0.85f));
    keys.setColour (juce::MidiKeyboardComponent::shadowColourId, bg.withAlpha (0.5f));
    keys.setColour (juce::MidiKeyboardComponent::textLabelColourId, bg);
    keys.setWantsKeyboardFocus (false);
    addAndMakeVisible (keys);
}

void KeyboardStrip::resized()
{
    auto r = getLocalBounds().reduced (12, 10).withTrimmedTop (4);
    keys.setBounds (r);
    keys.setKeyWidth (static_cast<float> (r.getWidth()) / 36.0f);   // 36 white keys = C1..C6
}

void KeyboardStrip::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (panel);
    g.fillRoundedRectangle (r, 4.0f);
    g.setGradientFill (juce::ColourGradient (orange, 0.0f, 0.0f, cyan, r.getRight(), 0.0f, false));
    g.fillRoundedRectangle (r.withHeight (4.0f), 2.0f);
}
