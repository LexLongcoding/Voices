// Voices - Copyright (C) 2026 LexLongcoding
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    juce::String vowelText (float v)
    {
        static const char* names[] = { "A", "E", "I", "O", "U" };
        const int i = juce::jlimit (0, 4, juce::roundToInt (v));
        if (std::abs (v - static_cast<float> (i)) < 0.06f)
            return names[i];
        const int lo = juce::jlimit (0, 3, static_cast<int> (v));
        return juce::String (names[lo]) + ">" + names[lo + 1] + " " + juce::String (juce::roundToInt ((v - static_cast<float> (lo)) * 100.0f)) + "%";
    }

    juce::AudioParameterFloatAttributes attributesFor (const Params::Spec& spec)
    {
        using U = Params::Unit;
        auto a = juce::AudioParameterFloatAttributes();
        switch (spec.unit)
        {
            case U::Time:
                return a.withLabel ("s")
                    .withStringFromValueFunction ([] (float v, int)
                    {
                        if (v <= 0.0005f) return juce::String ("0 ms");
                        return v < 1.0f ? juce::String (juce::roundToInt (v * 1000.0f)) + " ms" : juce::String (v, 2) + " s";
                    })
                    .withValueFromStringFunction ([] (const juce::String& t)
                    {
                        const float v = t.getFloatValue();
                        return t.containsIgnoreCase ("ms") ? v / 1000.0f : v;
                    });
            case U::Hz:
                return a.withLabel ("Hz")
                    .withStringFromValueFunction ([] (float v, int) { return juce::String (v, 2) + " Hz"; })
                    .withValueFromStringFunction ([] (const juce::String& t) { return t.getFloatValue(); });
            case U::Semitones:
                return a.withLabel ("st")
                    .withStringFromValueFunction ([] (float v, int)
                    {
                        const float r = std::round (v * 10.0f) / 10.0f;
                        return (r > 0.0f ? "+" : "") + juce::String (r, std::abs (r - std::round (r)) < 0.05f ? 0 : 1) + " st";
                    })
                    .withValueFromStringFunction ([] (const juce::String& t) { return t.getFloatValue(); });
            case U::Toggle:
                return a.withLabel ("")
                    .withStringFromValueFunction ([] (float v, int) { return juce::String (v >= 0.5f ? "ON" : "OFF"); })
                    .withValueFromStringFunction ([] (const juce::String& t)
                    {
                        return t.trim().equalsIgnoreCase ("on") || t.getFloatValue() >= 0.5f ? 1.0f : 0.0f;
                    });
            case U::Integer:
                return a.withLabel ("")
                    .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)); })
                    .withValueFromStringFunction ([] (const juce::String& t) { return t.getFloatValue(); });
            case U::Choice:
            {
                const int set = spec.choices;
                return a.withLabel ("")
                    .withStringFromValueFunction ([set] (float v, int)
                    {
                        int count = 0;
                        const auto* labels = Params::choiceLabels (set, count);
                        const int i = juce::jlimit (0, juce::jmax (0, count - 1), juce::roundToInt (v));
                        return labels != nullptr ? juce::String (labels[i]) : juce::String (i);
                    })
                    .withValueFromStringFunction ([set] (const juce::String& t)
                    {
                        int count = 0;
                        const auto* labels = Params::choiceLabels (set, count);
                        for (int i = 0; i < count; ++i)
                            if (t.trim().equalsIgnoreCase (labels[i])) return static_cast<float> (i);
                        return t.getFloatValue();
                    });
            }
            case U::Vowel:
                return a.withLabel ("")
                    .withStringFromValueFunction ([] (float v, int) { return vowelText (v); })
                    .withValueFromStringFunction ([] (const juce::String& t)
                    {
                        const auto s = t.trim().toUpperCase();
                        const int i = juce::String ("AEIOU").indexOf (s.substring (0, 1));
                        return i >= 0 && s.length() == 1 ? static_cast<float> (i) : t.getFloatValue();
                    });
            case U::Percent:
                break;
        }
        return a.withLabel ("%")
            .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; })
            .withValueFromStringFunction ([] (const juce::String& t) { return t.getFloatValue() / 100.0f; });
    }
}

juce::AudioProcessor::BusesProperties VoicesAudioProcessor::makeBuses()
{
    using AP = juce::AudioProcessor;
    const auto stereo = juce::AudioChannelSet::stereo();
   #if VOICES_FX
    return AP::BusesProperties().withInput ("Input", stereo, true).withOutput ("Output", stereo, true);
   #else
    // Instrument: a side-chain "Voice In" for the vocoder / auto-tune. The Standalone
    // app uses the main input instead, so your microphone just works there.
    const bool standalone = juce::PluginHostType::getPluginLoadedAs() == AP::wrapperType_Standalone;
    auto b = AP::BusesProperties().withOutput ("Output", stereo, true)
                                  .withInput ("Input", stereo, standalone);
    if (! standalone)
        b = b.withInput ("Voice In", stereo, true);
    return b;
   #endif
}

juce::AudioProcessorValueTreeState::ParameterLayout VoicesAudioProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (const auto& s : Params::specs)
    {
        juce::NormalisableRange<float> range (s.min, s.max);
        if (s.centre > 0.0f)
            range.setSkewForCentre (s.centre);
        if (s.unit == Params::Unit::Integer || s.unit == Params::Unit::Toggle || s.unit == Params::Unit::Choice)
            range.interval = 1.0f;
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { s.id, 1 }, s.name,
                                                                 range, s.def, attributesFor (s)));
    }
    return layout;
}

int VoicesAudioProcessor::paramIndexForId (const juce::String& id)
{
    for (int i = 0; i < Params::numParams; ++i)
        if (id == Params::specs[i].id)
            return i;
    jassertfalse;
    return 0;
}

VoicesAudioProcessor::VoicesAudioProcessor()
    : AudioProcessor (makeBuses()),
      apvts (*this, nullptr, "VOICES", createLayout())
{
    for (int i = 0; i < Params::numParams; ++i)
        raw[static_cast<size_t> (i)] = apvts.getRawParameterValue (Params::specs[i].id);

    auto I = [] (const char* id) { return paramIndexForId (id); };
    ix = { I ("master_level"), I ("master_voicetype"), I ("voice_level"),
           I ("tune_on"), I ("tune_key"), I ("tune_scale"), I ("tune_source"), I ("tune_retune"), I ("tune_amount"), I ("tune_humanize"),
           I ("chg_on"), I ("chg_amount"), I ("chg_pitch"), I ("chg_formant"), I ("chg_width"),
           I ("voc_on"), I ("voc_bands"), I ("voc_carrier"), I ("voc_release"), I ("voc_formant"), I ("voc_sibilance"), I ("voc_auto"), I ("voc_level"),
           I ("choir_on"), I ("choir_vowel"), I ("choir_size"), I ("choir_breath"), I ("choir_vibrato"), I ("choir_attack"), I ("choir_release"), I ("choir_level"),
           I ("vsyn_on"), I ("vsyn_vowel"), I ("vsyn_vibrato"), I ("vsyn_breath"), I ("vsyn_bright"), I ("vsyn_glide"), I ("vsyn_attack"), I ("vsyn_release"), I ("vsyn_level"),
           I ("arp_on"), I ("arp_mode"), I ("arp_octaves"), I ("arp_rate"), I ("arp_gate"), I ("arp_latch"),
           I ("chorus_rate"), I ("chorus_depth"), I ("chorus_mix"), I ("delay_time"), I ("delay_feedback"), I ("delay_mix"),
           I ("reverb_size"), I ("reverb_damp"), I ("reverb_mix") };
}

void VoicesAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, isFxBuild);
    updateSettings();
    engine.reset();
    setLatencySamples (isFxBuild ? engine.getLatency() : 0);
    events.reserve (1024);
    monoIn.assign (static_cast<size_t> (juce::jmax (1, samplesPerBlock)), 0.0f);
    keyDown.fill (false);
    sustained.fill (false);
    sustainPedal = false;
    keyboardState.reset();
}

bool VoicesAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    for (const auto& in : layouts.inputBuses)
        if (! in.isDisabled() && in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo())
            return false;
   #if VOICES_FX
    if (layouts.getMainInputChannelSet().isDisabled())
        return false;
   #endif
    return true;
}

void VoicesAudioProcessor::updateSettings()
{
    using VT = Formants::VoiceType;
    VoicesEngine::Settings s;
    s.voiceType   = static_cast<VT> (juce::jlimit (0, 2, juce::roundToInt (pv (ix.voiceType))));
    s.masterLevel = pv (ix.masterLevel);
    s.voiceLevel  = pv (ix.voiceLevel);

    s.tuneOn       = pv (ix.tuneOn) >= 0.5f;
    s.tuneKey      = juce::roundToInt (pv (ix.tuneKey));
    s.tuneScale    = juce::roundToInt (pv (ix.tuneScale));
    s.tuneFromMidi = pv (ix.tuneSource) >= 0.5f;
    s.retune       = pv (ix.tuneRetune);
    s.amount       = pv (ix.tuneAmount);
    s.humanize     = pv (ix.tuneHumanize);

    s.changerOn      = pv (ix.chgOn) >= 0.5f;
    s.changerAmount  = pv (ix.chgAmount);
    s.changerPitch   = pv (ix.chgPitch);
    s.changerFormant = pv (ix.chgFormant);
    s.changerWidth   = pv (ix.chgWidth);

    s.vocoderOn    = pv (ix.vocOn) >= 0.5f;
    s.vocoderAuto  = pv (ix.vocAuto) >= 0.5f;
    s.vocoderLevel = pv (ix.vocLevel);
    s.vocoder.bands        = juce::roundToInt (pv (ix.vocBands));
    s.vocoder.carrier      = static_cast<Vocoder::Carrier> (juce::jlimit (0, 2, juce::roundToInt (pv (ix.vocCarrier))));
    s.vocoder.release      = pv (ix.vocRelease);
    s.vocoder.formantSemis = pv (ix.vocFormant);
    s.vocoder.sibilance    = pv (ix.vocSibilance);

    s.choirOn    = pv (ix.choirOn) >= 0.5f;
    s.choirLevel = pv (ix.choirLevel);
    s.choir.vowel   = pv (ix.choirVowel);
    s.choir.size    = pv (ix.choirSize);
    s.choir.breath  = pv (ix.choirBreath);
    s.choir.vibrato = pv (ix.choirVibrato);
    s.choir.attack  = pv (ix.choirAttack);
    s.choir.release = pv (ix.choirRelease);

    s.vocalOn    = pv (ix.vsynOn) >= 0.5f;
    s.vocalLevel = pv (ix.vsynLevel);
    s.vocal.vowel   = pv (ix.vsynVowel);
    s.vocal.vibrato = pv (ix.vsynVibrato);
    s.vocal.breath  = pv (ix.vsynBreath);
    s.vocal.bright  = pv (ix.vsynBright);
    s.vocal.glide   = pv (ix.vsynGlide);
    s.vocal.attack  = pv (ix.vsynAttack);
    s.vocal.release = pv (ix.vsynRelease);

    s.arpOn = pv (ix.arpOn) >= 0.5f;
    s.arp.mode      = static_cast<Arpeggiator::Mode> (juce::jlimit (0, 3, juce::roundToInt (pv (ix.arpMode))));
    s.arp.octaves   = juce::roundToInt (pv (ix.arpOctaves));
    s.arp.rateBeats = Params::arpRateBeats[juce::jlimit (0, 5, juce::roundToInt (pv (ix.arpRate)))];
    s.arp.gate      = pv (ix.arpGate);
    s.arp.latch     = pv (ix.arpLatch) >= 0.5f;

    s.chorusRate    = pv (ix.chorusRate);
    s.chorusDepth   = pv (ix.chorusDepth);
    s.chorusMix     = pv (ix.chorusMix);
    s.delayBeats    = Params::delayTimeBeats[juce::jlimit (0, 7, juce::roundToInt (pv (ix.delayTime)))];
    s.delayFeedback = pv (ix.delayFeedback);
    s.delayMix      = pv (ix.delayMix);
    s.reverbSize    = pv (ix.reverbSize);
    s.reverbDamp    = pv (ix.reverbDamp);
    s.reverbMix     = pv (ix.reverbMix);
    engine.setSettings (s);
}

void VoicesAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    updateSettings();

    // the on-screen keyboard adds its notes to the incoming MIDI
    keyboardState.processNextMidiBuffer (midi, 0, n, true);

    // ---- voice input: average of each active input bus, summed (main and/or side-chain)
    if (static_cast<int> (monoIn.size()) < n) monoIn.resize (static_cast<size_t> (n));
    std::fill (monoIn.begin(), monoIn.begin() + n, 0.0f);
    bool anyInput = false;
    for (int b = 0; b < getBusCount (true); ++b)
    {
        auto bus = getBusBuffer (buffer, true, b);
        const int chans = bus.getNumChannels();
        if (chans == 0) continue;
        anyInput = true;
        const float g = 1.0f / static_cast<float> (chans);
        for (int c = 0; c < chans; ++c)
            juce::FloatVectorOperations::addWithMultiply (monoIn.data(), bus.getReadPointer (c), g, n);
    }
    readout.hasInput.store (anyInput, std::memory_order_relaxed);
    const auto range = juce::FloatVectorOperations::findMinAndMax (monoIn.data(), n);
    readout.inLevel.store (juce::jmax (-range.getStart(), range.getEnd()), std::memory_order_relaxed);

    // ---- MIDI -> engine events (with sustain pedal)
    events.clear();
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        const int pos = juce::jlimit (0, juce::jmax (0, n - 1), meta.samplePosition);
        using E = VoicesEngine::MidiEvent;
        if (m.isNoteOn())
        {
            const int note = m.getNoteNumber();
            keyDown[static_cast<size_t> (note)] = true;
            sustained[static_cast<size_t> (note)] = false;
            events.push_back ({ pos, E::noteOn, note, m.getFloatVelocity() });
        }
        else if (m.isNoteOff())
        {
            const int note = m.getNoteNumber();
            keyDown[static_cast<size_t> (note)] = false;
            if (sustainPedal) sustained[static_cast<size_t> (note)] = true;
            else events.push_back ({ pos, E::noteOff, note, 0.0f });
        }
        else if (m.isSustainPedalOn())  sustainPedal = true;
        else if (m.isSustainPedalOff())
        {
            sustainPedal = false;
            for (int k = 0; k < 128; ++k)
                if (sustained[static_cast<size_t> (k)])
                {
                    sustained[static_cast<size_t> (k)] = false;
                    events.push_back ({ pos, E::noteOff, k, 0.0f });
                }
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            keyDown.fill (false); sustained.fill (false);
            events.push_back ({ pos, E::allOff, 0, 0.0f });
        }
    }

    VoicesEngine::Transport tp;
    if (auto* ph = getPlayHead())
        if (const auto pos = ph->getPosition())
        {
            if (const auto bpm = pos->getBpm()) tp.bpm = *bpm;
            tp.playing = pos->getIsPlaying();
            if (const auto ppq = pos->getPpqPosition()) tp.ppq = *ppq;
            else tp.playing = false;
        }

    const float* in[1] = { monoIn.data() };
    auto* outL = buffer.getWritePointer (0);
    auto* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : outL;
    engine.process (in, 1, outL, outR, n, events.data(), static_cast<int> (events.size()), tp);
    for (int c = 2; c < buffer.getNumChannels(); ++c)
        buffer.clear (c, 0, n);   // (input-only channels beyond the stereo output)

    // ---- read-outs for the editor
    readout.voiced.store (engine.isVoiced(), std::memory_order_relaxed);
    readout.inputMidi.store (engine.getInputMidi(), std::memory_order_relaxed);
    readout.outputMidi.store (engine.getOutputMidi(), std::memory_order_relaxed);
    readout.targetNote.store (engine.getTargetNote(), std::memory_order_relaxed);
    readout.arpStep.store (engine.getArpStep(), std::memory_order_relaxed);
    readout.out[0].store (buffer.getMagnitude (0, 0, n), std::memory_order_relaxed);
    readout.out[1].store (buffer.getMagnitude (buffer.getNumChannels() > 1 ? 1 : 0, 0, n), std::memory_order_relaxed);
    const auto& voc = engine.getVocoder();
    readout.numBands.store (voc.getNumBands(), std::memory_order_relaxed);
    for (int b = 0; b < voc.getNumBands(); ++b)
        readout.bands[static_cast<size_t> (b)].store (voc.getBandLevel (b), std::memory_order_relaxed);
}

juce::AudioProcessorEditor* VoicesAudioProcessor::createEditor()
{
    return new VoicesAudioProcessorEditor (*this);
}

//==============================================================================
void VoicesAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty (uiScaleProperty, uiScale, nullptr);
    state.setProperty (patchNameProperty, patches.getCurrentName(), nullptr);
    state.setProperty (patchModifiedProperty, patches.isModified(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void VoicesAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            uiScale = juce::jlimit (0.5f, 1.25f, static_cast<float> (state.getProperty (uiScaleProperty, 0.8f)));
            const auto patchName = state.getProperty (patchNameProperty).toString();
            const bool patchModified = state.getProperty (patchModifiedProperty, patchName.isEmpty());
            for (auto* p : { uiScaleProperty, patchNameProperty, patchModifiedProperty })
                state.removeProperty (p, nullptr);
            apvts.replaceState (state);
            patches.restoreSession (patchName, patchModified);   // after replaceState, which marks "modified"
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VoicesAudioProcessor();
}
