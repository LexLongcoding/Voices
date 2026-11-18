// Voices - Copyright (C) 2026 LexLongcoding
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "PluginEditor.h"

using namespace Theme;

namespace
{
    // design-canvas layout
    constexpr int row1Y = 115, row1H = 342;
    constexpr int row2Y = 492, row2H = 272;
    constexpr int keysY = 776, keysH = 108;
}

//==============================================================================
void LcdDisplay::setPatchText (const juce::String& t)
{
    if (t != patchText) { patchText = t; if (! showingMessage()) repaint(); }
}

void LcdDisplay::showMessage (const juce::String& t, juce::Colour c, int holdMs)
{
    message = t;
    messageColour = c;
    persistent = holdMs < 0;
    messageUntil = juce::Time::getMillisecondCounter() + static_cast<juce::uint32> (juce::jmax (0, holdMs));
    repaint();
}

void LcdDisplay::clearMessage() { message.clear(); persistent = false; repaint(); }

bool LcdDisplay::showingMessage() const
{
    return message.isNotEmpty() && (persistent || juce::Time::getMillisecondCounter() < messageUntil);
}

void LcdDisplay::tick()
{
    if (message.isNotEmpty() && ! showingMessage())
        clearMessage();
}

void LcdDisplay::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (lcdBg);
    g.fillRoundedRectangle (r, 3.0f);
    for (int i = 1; i <= 3; ++i)   // inner glow
    {
        g.setColour (cyan.withAlpha (0.06f));
        g.drawRoundedRectangle (r.reduced (static_cast<float> (i) * 1.5f), 3.0f, 2.0f);
    }
    g.setColour (cyan);
    g.drawRoundedRectangle (r, 3.0f, 1.0f);
    const bool msg = showingMessage();
    g.setColour (msg ? messageColour : cyan);
    g.setFont (mono (13.0f));
    g.drawFittedText ((msg ? message : patchText).toUpperCase(), getLocalBounds().reduced (12, 0),
                      juce::Justification::centredLeft, 1, 0.8f);
    if (! msg)
    {
        juce::Path arrow;
        const float ax = static_cast<float> (getWidth()) - 14.0f, ay = static_cast<float> (getHeight()) * 0.5f;
        arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
        g.setColour (cyan.withAlpha (0.6f));
        g.fillPath (arrow);
    }
}

//==============================================================================
InputBadge::InputBadge (VoicesAudioProcessor& p) : proc (p)
{
    setTooltip (VoicesAudioProcessor::isFxBuild
                    ? "The track's audio feeds auto-tune, the voice changer and the vocoder."
                    : "Voice input for auto-tune, the voice changer and the vocoder: the microphone in the "
                      "Standalone app, or the plug-in's side-chain input in your DAW.");
}

void InputBadge::tick()
{
    const bool in = proc.readout.hasInput.load();
    const float db = juce::Decibels::gainToDecibels (proc.readout.inLevel.load(), -60.0f);
    const float target = juce::jlimit (0.0f, 1.0f, (db + 54.0f) / 54.0f);
    const float next = juce::jmax (target, level * 0.85f);
    if (in != hasInput || std::abs (next - level) > 0.004f) { hasInput = in; level = next; repaint(); }
}

void InputBadge::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (lcdBg);
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (orange.withAlpha (0.8f));
    g.drawRoundedRectangle (r, 3.0f, 1.0f);

    auto inner = getLocalBounds().reduced (10, 0);
    g.setFont (mono (11.0f));
    g.setColour (text);
    g.drawText ("VOICE IN", inner.removeFromLeft (64), juce::Justification::centredLeft, false);

    auto right = inner.removeFromRight (64);
    g.setColour (dim);
    g.drawText (VoicesAudioProcessor::isFxBuild ? juce::String (proc.getLatencyMs()) + " MS" : "", right,
                juce::Justification::centredRight, false);

    auto bar = inner.reduced (4, 0).withSizeKeepingCentre (inner.getWidth() - 8, 8).toFloat();
    if (! hasInput)
    {
        g.setColour (dim);
        const bool sideChain = ! VoicesAudioProcessor::isFxBuild && proc.wrapperType != juce::AudioProcessor::wrapperType_Standalone;
        g.drawText (sideChain ? "SIDE-CHAIN OFF" : "NO INPUT", inner, juce::Justification::centred, false);
        return;
    }
    g.setColour (orange.withAlpha (0.12f));
    g.fillRoundedRectangle (bar, 2.0f);
    juce::ColourGradient grad (cyan, bar.getX(), 0.0f, pink, bar.getRight(), 0.0f, false);
    grad.addColour (0.7, orange);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * level), 2.0f);
}

//==============================================================================
LearnOverlay::LearnOverlay (UiContext& c) : ctx (c)
{
    setInterceptsMouseClicks (true, false);
}

std::vector<LearnOverlay::Target> LearnOverlay::targets()
{
    std::vector<Target> out;
    auto* root = getParentComponent();
    if (root == nullptr) return out;

    std::function<void (juce::Component&)> walk = [&] (juce::Component& parent)
    {
        for (auto* c : parent.getChildren())
        {
            if (c == this || ! c->isVisible()) continue;   // (hidden parents hide their children)
            if (auto* l = dynamic_cast<LearnableControl*> (c))
                out.push_back ({ l->getLearnParam(), getLocalArea (c, c->getLocalBounds()) });
            else
                walk (*c);
        }
    };
    walk (*root);
    return out;
}

void LearnOverlay::paint (juce::Graphics& g)
{
    using namespace Theme;
    g.fillAll (bg.withAlpha (0.45f));
    const int armedParam = ctx.proc.getArmedCcParam();

    for (const auto& t : targets())
    {
        const bool armed = t.param == armedParam;
        const int cc = ctx.proc.ccForParam (t.param);
        const bool mapped = cc >= 0;

        const auto rf = t.bounds.toFloat().reduced (1.0f);
        if (armed)
        {
            g.setColour (learn.withAlpha (0.28f));
            g.fillRoundedRectangle (rf, 4.0f);
        }
        g.setColour (learn.withAlpha (armed ? 1.0f : (mapped ? 0.85f : 0.4f)));
        g.drawRoundedRectangle (rf, 4.0f, armed ? 2.5f : 1.2f);

        // knobs carry the badge inside; on short controls (buttons, menus) it would hide
        // their text, so it sits just above them and is left out while unassigned
        const bool shortControl = t.bounds.getHeight() < 40;
        if (shortControl && ! mapped && ! armed)
            continue;
        const juce::String badge = armed ? "MOVE CC" : (mapped ? "CC " + juce::String (cc) : juce::String ("--"));
        g.setFont (mono (10.0f));
        const int bw = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), badge) + 8;
        const int by = shortControl ? t.bounds.getY() - 14 : t.bounds.getY() + 2;
        auto br = juce::Rectangle<int> (t.bounds.getRight() - bw - 2, by, bw, 13).toFloat();
        g.setColour (mapped || armed ? learn : panelLo);
        g.fillRoundedRectangle (br, 3.0f);
        g.setColour (mapped || armed ? bg : dim);
        g.drawText (badge, br.toNearestInt(), juce::Justification::centred, false);
    }
}

void LearnOverlay::mouseDown (const juce::MouseEvent& e)
{
    // smallest target under the mouse wins (a knob drawn inside a larger control)
    const Target* hit = nullptr;
    const auto all = targets();
    for (const auto& t : all)
        if (t.bounds.contains (e.getPosition())
             && (hit == nullptr || t.bounds.getWidth() * t.bounds.getHeight() < hit->bounds.getWidth() * hit->bounds.getHeight()))
            hit = &t;

    if (hit == nullptr)   // empty space
    {
        ctx.proc.cancelLearning();
        if (onCancelled) onCancelled();
    }
    else if (e.mods.isPopupMenu())   // right-click clears that mapping
    {
        ctx.proc.clearCcForParam (hit->param);
        if (onCleared) onCleared (hit->param);
    }
    else if (ctx.proc.getArmedCcParam() == hit->param)   // second click disarms
    {
        ctx.proc.cancelLearning();
        if (onCancelled) onCancelled();
    }
    else
    {
        ctx.proc.armCcLearn (hit->param);
        if (onArmed) onArmed (hit->param);
    }
    repaint();
}

//==============================================================================
void VoicesAudioProcessorEditor::Canvas::paint (juce::Graphics& g)
{
    if (background.isNull())
    {
        background = juce::Image (juce::Image::RGB, designWidth, designHeight, true);
        juce::Graphics bgG (background);
        bgG.fillAll (bg);
        bgG.setColour (cyan.withAlpha (0.035f));   // scanlines
        for (int y = 0; y < designHeight; y += 3)
            bgG.fillRect (0, y, designWidth, 1);

        // logo: VOICES in pink with an orange tail, like DECU / 6000
        auto logo = juce::Rectangle<int> (20, 20, 420, 40);
        bgG.setFont (display (32.0f, true));
        const juce::String a ("VOI"), b ("CES");
        const int w = juce::GlyphArrangement::getStringWidthInt (bgG.getCurrentFont(), a);
        bgG.setColour (pink.withAlpha (0.35f));
        bgG.drawText (a, logo.translated (0, 1), juce::Justification::centredLeft, false);
        bgG.setColour (pink);
        bgG.drawText (a, logo, juce::Justification::centredLeft, false);
        bgG.setColour (orange);
        bgG.drawText (b, logo.withTrimmedLeft (w), juce::Justification::centredLeft, false);
        const int w2 = juce::GlyphArrangement::getStringWidthInt (bgG.getCurrentFont(), a + b);
        if (isFx)
        {
            bgG.setColour (cyan);
            bgG.setFont (display (14.0f, true));
            bgG.drawText ("FX", logo.withTrimmedLeft (w2 + 8).withTrimmedTop (4), juce::Justification::topLeft, false);
        }
        bgG.setColour (text);
        bgG.setFont (mono (11.0f));
        bgG.drawText ("VOCAL / CHOIR SYNTHESIZER // VOCODER // AUTO-TUNE", 21, 60, 460, 14, juce::Justification::centredLeft, false);

        // tri-colour bar
        const int barY = 82, x0 = 20, bw = designWidth - 40;
        bgG.setColour (orange); bgG.fillRect (x0, barY, bw * 3 / 6, 3);
        bgG.setColour (pink);   bgG.fillRect (x0 + bw * 3 / 6, barY, bw * 2 / 6, 3);
        bgG.setColour (cyan);   bgG.fillRect (x0 + bw * 5 / 6, barY, bw - bw * 5 / 6, 3);

        // section headings
        auto heading = [&] (int y, const juce::String& t, juce::Colour c)
        {
            bgG.setFont (mono (11.0f));
            bgG.setColour (c);
            const int tw = juce::GlyphArrangement::getStringWidthInt (bgG.getCurrentFont(), t);
            bgG.drawText (t, 20, y, tw + 4, 14, juce::Justification::centredLeft, false);
            bgG.setColour (c.withAlpha (0.35f));
            bgG.fillRect (20 + tw + 14, y + 7, designWidth - 40 - tw - 14, 1);
        };
        heading (97, "VOICE INPUT // PITCH ENGINE", text);
        heading (row2Y - 18, "SYNTH VOICES // PLAY FROM MIDI", cyan);
    }
    g.drawImageAt (background, 0, 0);
}

//==============================================================================
VoicesAudioProcessorEditor::VoicesAudioProcessorEditor (VoicesAudioProcessor& p)
    : AudioProcessorEditor (p), voices (p), ctx (p),
      canvas (VoicesAudioProcessor::isFxBuild),
      voiceType (ctx, "master_voicetype", { "MALE", "FEMALE", "MIXED" }, cyan),
      inputBadge (p),
      tune (ctx), changer (ctx), vocoder (ctx), choir (ctx), vocal (ctx), arp (ctx), fx (ctx), keyboard (ctx),
      panels { &tune, &changer, &vocoder, &choir, &vocal, &arp, &fx },
      overlay (ctx)
{
    setLookAndFeel (&lnf);
    setWantsKeyboardFocus (true);
    ctx.setLcd   = [this] (const juce::String& t, juce::Colour c) { setLcd (t, c); };
    ctx.armLearn = [this] (int param) { armLearn (param); };

    addAndMakeVisible (canvas);
    canvas.setBounds (0, 0, designWidth, designHeight);

    voiceType.gap = 6;
    canvas.addAndMakeVisible (voiceType);
    canvas.addAndMakeVisible (lcd);
    lcd.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    lcd.onClick = [this] { showPatchMenu(); };
    prevPatch.onClick = [this] { voices.patches.loadNext (-1); updatePatchText(); lcd.clearMessage(); };
    nextPatch.onClick = [this] { voices.patches.loadNext (+1); updatePatchText(); lcd.clearMessage(); };
    canvas.addAndMakeVisible (prevPatch);
    canvas.addAndMakeVisible (nextPatch);
    canvas.addAndMakeVisible (inputBadge);
    for (auto* panel : panels)
        canvas.addAndMakeVisible (panel);
    canvas.addAndMakeVisible (keyboard);
    updatePatchText();

    // MIDI learn
    learnButton.setClickingTogglesState (true);
    learnButton.setColour (juce::TextButton::buttonOnColourId, learn);
    learnButton.setTooltip ("MIDI LEARN: click a control, then move a knob or fader on your controller. "
                            "Right-click a control to clear it. Right-click this button for more.");
    learnButton.onClick = [this] { setLearnMode (learnButton.getToggleState()); };
    learnButton.onRightClick = [this] { showLearnMenu(); };
    canvas.addAndMakeVisible (learnButton);

    canvas.addChildComponent (overlay);
    overlay.setBounds (0, 0, designWidth, designHeight);
    overlay.onArmed     = [this] (int param) { setLcd ("LEARN: MOVE A KNOB OR FADER FOR " + paramTitleOf (param), learn); };
    overlay.onCleared   = [this] (int param) { setLcd (paramTitleOf (param) + "  //  MIDI CC CLEARED", learn); };
    overlay.onCancelled = [this] { setLcd ("LEARN: CLICK A CONTROL  //  ESC TO EXIT", learn); };
    seenCcLearnCount = voices.getCcLearnCount();

    sendLookAndFeelChange();

    const float savedScale = juce::jlimit (0.5f, 1.25f, voices.uiScale);
    setResizable (true, true);
    setResizeLimits (designWidth / 2, designHeight / 2, designWidth * 5 / 4, designHeight * 5 / 4);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio (static_cast<double> (designWidth) / designHeight);
    setSize (juce::roundToInt (designWidth * savedScale), juce::roundToInt (designHeight * savedScale));
    startTimerHz (30);
}

VoicesAudioProcessorEditor::~VoicesAudioProcessorEditor()
{
    voices.cancelLearning();
    setLookAndFeel (nullptr);
}

void VoicesAudioProcessorEditor::setLcd (const juce::String& t, juce::Colour c)
{
    // learn prompts stay up while learn mode is on or a control is waiting for its CC
    const bool hold = c == learn && (ctx.learnMode || voices.getArmedCcParam() >= 0);
    lcd.showMessage (t, c, hold ? -1 : 2500);
}

void VoicesAudioProcessorEditor::timerCallback()
{
    updatePatchText();
    lcd.tick();
    inputBadge.tick();
    for (auto* panel : panels)
        panel->tick();

    bool learnChanged = false;
    if (const int n = voices.getCcLearnCount(); n != seenCcLearnCount)
    {
        seenCcLearnCount = n;
        learnChanged = true;
        const int cc = voices.getLastLearnedCc();
        const int param = voices.paramForCc (cc);
        if (param >= 0)
            setLcd (paramTitleOf (param) + " <- CC " + juce::String (cc), learn);
    }
    if (overlay.isVisible() && (learnChanged || ++tickCount % 3 == 0))
        overlay.repaint();
}

//==============================================================================
// MIDI learn (as in Decu6000)
void VoicesAudioProcessorEditor::setLearnMode (bool on)
{
    ctx.learnMode = on;
    learnButton.setToggleState (on, juce::dontSendNotification);
    voices.cancelLearning();
    overlay.setVisible (on);
    overlay.toFront (false);
    learnButton.toFront (false);   // stays clickable above the overlay
    if (on)
    {
        setLcd ("LEARN: CLICK A CONTROL  //  ESC TO EXIT", learn);
        if (isShowing()) grabKeyboardFocus();
    }
    else
    {
        lcd.clearMessage();
    }
}

void VoicesAudioProcessorEditor::armLearn (int param)
{
    voices.armCcLearn (param);
    setLcd ("LEARN: MOVE A KNOB OR FADER FOR " + paramTitleOf (param) + "  //  ESC CANCELS", learn);
    if (isShowing()) grabKeyboardFocus();
}

void VoicesAudioProcessorEditor::showLearnMenu()
{
    const int mapped = voices.getNumMappedCcs();
    juce::PopupMenu m;
    m.addSectionHeader ("MIDI LEARN");
    m.addItem (1, ctx.learnMode ? "Exit learn mode" : "Enter learn mode");
    m.addItem (2, "Clear all MIDI CC assignments" + (mapped > 0 ? " (" + juce::String (mapped) + ")" : juce::String()), mapped > 0);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&learnButton),
                     [safe = juce::Component::SafePointer<VoicesAudioProcessorEditor> (this)] (int r)
                     {
                         if (safe == nullptr) return;
                         if (r == 1) safe->setLearnMode (! safe->ctx.learnMode);
                         if (r == 2)
                         {
                             safe->voices.clearAllCcs();
                             safe->setLcd ("ALL MIDI CC ASSIGNMENTS CLEARED", learn);
                             safe->overlay.repaint();
                         }
                     });
}

bool VoicesAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    if (key != juce::KeyPress::escapeKey)
        return false;
    if (voices.getArmedCcParam() >= 0)
    {
        voices.cancelLearning();
        setLcd (ctx.learnMode ? "LEARN: CANCELLED  //  CLICK A CONTROL" : "LEARN: CANCELLED", learn);
        overlay.repaint();
        return true;
    }
    if (ctx.learnMode)
    {
        setLearnMode (false);
        return true;
    }
    return false;
}

void VoicesAudioProcessorEditor::resized()
{
    if (getWidth() <= 0)
        return;
    const float scale = static_cast<float> (getWidth()) / static_cast<float> (designWidth);
    canvas.setTransform (juce::AffineTransform::scale (scale));
    voices.uiScale = scale;

    // header, right-aligned
    auto header = juce::Rectangle<int> (20, 28, designWidth - 40, 34);
    learnButton.setBounds (header.removeFromRight (116));
    header.removeFromRight (12);
    inputBadge.setBounds (header.removeFromRight (230));
    header.removeFromRight (16);
    nextPatch.setBounds (header.removeFromRight (26).reduced (4, 8));
    header.removeFromRight (2);
    lcd.setBounds (header.removeFromRight (320));
    header.removeFromRight (2);
    prevPatch.setBounds (header.removeFromRight (26).reduced (4, 8));
    header.removeFromRight (20);
    voiceType.setBounds (header.removeFromRight (306));

    tune.setBounds    (20,  row1Y, 580, row1H);
    changer.setBounds (612, row1Y, 330, row1H);
    vocoder.setBounds (954, row1Y, 466, row1H);

    choir.setBounds (20,   row2Y, 350, row2H);
    vocal.setBounds (382,  row2Y, 370, row2H);
    arp.setBounds   (764,  row2Y, 250, row2H);
    fx.setBounds    (1026, row2Y, 394, row2H);

    keyboard.setBounds (20, keysY, designWidth - 40, keysH);
}

//==============================================================================
// Patches (same browser as Decu6000)
void VoicesAudioProcessorEditor::updatePatchText()
{
    auto& pm = voices.patches;
    const int n = pm.getDisplayNumber();
    lcd.setPatchText ("PATCH " + (n > 0 ? juce::String (n).paddedLeft ('0', 2) : juce::String ("--"))
                      + " // " + pm.getCurrentName() + (pm.isModified() ? " *" : ""));
}

void VoicesAudioProcessorEditor::showPatchMenu()
{
    auto& pm = voices.patches;
    pm.refresh();
    const auto& entries = pm.getEntries();

    enum { saveId = 1, saveAsId, deleteId, folderId, loadBase = 1000 };
    juce::PopupMenu m;
    m.addSectionHeader ("FACTORY");
    for (int i = 0; i < static_cast<int> (entries.size()); ++i)
        if (entries[static_cast<size_t> (i)].factory)
            m.addItem (loadBase + i, entries[static_cast<size_t> (i)].name, true, i == pm.getCurrentIndex());
    m.addSectionHeader ("USER");
    if (static_cast<int> (entries.size()) == pm.getNumFactory())
        m.addItem (-1, "(no user patches yet - use Save As)", false);
    for (int i = 0; i < static_cast<int> (entries.size()); ++i)
        if (! entries[static_cast<size_t> (i)].factory)
            m.addItem (loadBase + i, entries[static_cast<size_t> (i)].name, true, i == pm.getCurrentIndex());
    m.addSeparator();
    m.addItem (saveId, "Save \"" + pm.getCurrentName() + "\"", pm.isCurrentUserPatch());
    m.addItem (saveAsId, "Save As...");
    m.addItem (deleteId, "Delete \"" + pm.getCurrentName() + "\"...", pm.isCurrentUserPatch());
    m.addSeparator();
    m.addItem (folderId, "Open patch folder");

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&lcd),
                     [safe = juce::Component::SafePointer<VoicesAudioProcessorEditor> (this)] (int r)
                     {
                         if (safe == nullptr || r == 0) return;
                         auto& p = safe->voices.patches;
                         if (r >= loadBase)
                         {
                             if (! p.load (r - loadBase)) safe->setLcd ("COULDN'T READ THAT PATCH FILE", pink);
                             else                         safe->lcd.clearMessage();
                         }
                         else if (r == saveId)   safe->saveTo (p.getCurrentName());
                         else if (r == saveAsId) safe->saveAsDialog();
                         else if (r == deleteId) safe->confirmDelete();
                         else if (r == folderId)
                         {
                             const auto folder = PatchManager::getUserFolder();
                             folder.createDirectory();
                             folder.startAsProcess();
                         }
                         safe->updatePatchText();
                     });
}

void VoicesAudioProcessorEditor::saveAsDialog()
{
    auto* w = new juce::AlertWindow ("SAVE PATCH", "Name for this patch:", juce::MessageBoxIconType::NoIcon, this);
    const auto& pm = voices.patches;
    w->addTextEditor ("name", pm.isCurrentUserPatch() || pm.getCurrentName() != "INIT" ? pm.getCurrentName() : "MY PATCH");
    w->addButton ("SAVE", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    w->enterModalState (true, juce::ModalCallbackFunction::create (
        [safe = juce::Component::SafePointer<VoicesAudioProcessorEditor> (this), w] (int result)
        {
            if (safe == nullptr || result != 1) return;
            const auto name = PatchManager::sanitiseName (w->getTextEditorContents ("name"));
            if (name.isEmpty()) { safe->setLcd ("PATCH NAME CAN'T BE EMPTY", pink); return; }

            if (safe->voices.patches.userPatchExists (name) && name != safe->voices.patches.getCurrentName())
            {
                juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                                  .withIconType (juce::MessageBoxIconType::QuestionIcon)
                                                  .withTitle ("REPLACE PATCH?")
                                                  .withMessage ("A patch called \"" + name + "\" already exists. Replace it?")
                                                  .withButton ("REPLACE").withButton ("CANCEL")
                                                  .withAssociatedComponent (safe.getComponent()),
                                              [safe, name] (int r) { if (safe != nullptr && r == 1) safe->saveTo (name); });
                return;
            }
            safe->saveTo (name);
        }), true);
}

void VoicesAudioProcessorEditor::saveTo (const juce::String& name)
{
    if (voices.patches.saveUser (name)) setLcd ("SAVED: " + PatchManager::sanitiseName (name), orange);
    else                                setLcd ("COULDN'T SAVE - CHECK THE PATCH FOLDER", pink);
    updatePatchText();
}

void VoicesAudioProcessorEditor::confirmDelete()
{
    const auto name = voices.patches.getCurrentName();
    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::WarningIcon)
                                      .withTitle ("DELETE PATCH?")
                                      .withMessage ("Delete the patch \"" + name + "\"? The current sound stays loaded.")
                                      .withButton ("DELETE").withButton ("CANCEL")
                                      .withAssociatedComponent (this),
                                  [safe = juce::Component::SafePointer<VoicesAudioProcessorEditor> (this), name] (int r)
                                  {
                                      if (safe == nullptr || r != 1) return;
                                      auto& pm = safe->voices.patches;
                                      if (pm.deleteUser (pm.getCurrentIndex()))
                                          safe->setLcd ("DELETED: " + name, orange);
                                      safe->updatePatchText();
                                  });
}
