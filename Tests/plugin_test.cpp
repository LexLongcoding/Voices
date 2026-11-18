// Voices - Copyright (C) 2026 LexLongcoding
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Plug-in level tests (built with -DVOICES_BUILD_TESTS=ON):
//   * the processor: side-chain voice + MIDI, every factory patch, state save / restore,
//     parameter text, sustain pedal, sample-rate / block-size changes
//   * the editor: renders screenshots while audio is running (docs/images)
//   * the built VST3s: loaded the way a DAW loads them, then played
//
//   ./VoicesTests <screenshot-folder> [path/to/Voices.vst3] [path/to/Voices FX.vst3]

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "FactoryPatches.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <cstdio>

namespace
{
    int failures = 0, passes = 0;
    void check (bool ok, const juce::String& what)
    {
        std::printf ("  [%s] %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8());
        (ok ? passes : failures)++;
    }

    constexpr double SR = 48000.0;
    constexpr int BS = 512;

    // a sung "aah" (Rosenberg pulses through male formants) wandering off-key
    struct Singer
    {
        double phase = 0.0, t = 0.0;
        Formants::Bank bank;
        Singer() { bank.prepare (SR); bank.setTarget (Formants::vowel (false, 0.0f)); }
        float next()
        {
            t += 1.0 / SR;
            const double notes[] = { 52.35, 55.6, 57.3, 59.5 };
            const double midi = notes[static_cast<int> (t / 0.8) % 4] + 0.25 * std::sin (2.0 * juce::MathConstants<double>::pi * 5.5 * t);
            phase += 440.0 * std::pow (2.0, (midi - 69.0) / 12.0) / SR;
            phase -= std::floor (phase);
            double g = phase < 0.4 ? 0.5 * (1.0 - std::cos (juce::MathConstants<double>::pi * phase / 0.4))
                     : phase < 0.6 ? std::cos (juce::MathConstants<double>::pi * (phase - 0.4) / 0.4) : 0.0;
            return bank.process (static_cast<float> (g) - 0.35f) * 2.5f;
        }
    };

    void setParam (juce::AudioProcessor&, VoicesAudioProcessor& v, const char* id, float value)
    {
        auto* prm = v.apvts.getParameter (id);
        prm->setValueNotifyingHost (prm->convertTo0to1 (value));
    }

    struct Stats { float peak = 0.0f; double rms = 0.0; bool finite = true; };

    // Runs the processor for `seconds`, voice into the given input channels, MIDI chord on/off.
    Stats run (juce::AudioProcessor& p, Singer* singer, double seconds, std::vector<int> chord, int voiceChannelOffset,
               std::function<void()> betweenBlocks = {}, bool releaseAtEnd = true)
    {
        Stats s;
        juce::AudioBuffer<float> buf (juce::jmax (p.getTotalNumInputChannels(), p.getTotalNumOutputChannels()), BS);
        const int blocks = static_cast<int> (seconds * SR / BS);
        double sumSq = 0.0; long count = 0;
        for (int b = 0; b < blocks; ++b)
        {
            buf.clear();
            if (singer != nullptr && p.getTotalNumInputChannels() > 0)
                for (int i = 0; i < BS; ++i)
                {
                    const float v = singer->next();
                    for (int c = 0; c < juce::jmin (2, p.getTotalNumInputChannels()); ++c)
                        buf.setSample (voiceChannelOffset + c, i, v);
                }
            juce::MidiBuffer midi;
            if (b == 0) for (int n : chord) midi.addEvent (juce::MidiMessage::noteOn (1, n, 0.8f), 10);
            if (releaseAtEnd && b == blocks - 1) for (int n : chord) midi.addEvent (juce::MidiMessage::noteOff (1, n), 0);
            p.processBlock (buf, midi);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < BS; ++i)
                {
                    const float v = buf.getSample (c, i);
                    if (! std::isfinite (v)) s.finite = false;
                    s.peak = juce::jmax (s.peak, std::abs (v));
                    sumSq += static_cast<double> (v) * v; ++count;
                }
            if (betweenBlocks) betweenBlocks();
        }
        s.rms = std::sqrt (sumSq / juce::jmax (1L, count));
        return s;
    }

    bool savePng (juce::Component& c, const juce::File& file)
    {
        const auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat png;
        return out.openedOk() && png.writeImageToStream (img, out);
    }

    void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File shots (argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]) : juce::File::getCurrentWorkingDirectory());
    shots.createDirectory();

    std::printf ("Voices plug-in tests\n\n1. Processor (instrument build)\n");
    VoicesAudioProcessor proc;
    {
        // DAW-style layout: main input off, side-chain on
        auto layout = proc.getBusesLayout();
        check (layout.inputBuses.size() == 2, "instrument has main + side-chain inputs");
        check (layout.getMainOutputChannelSet() == juce::AudioChannelSet::stereo(), "stereo output");
        proc.setRateAndBufferSizeDetails (SR, BS);
        proc.prepareToPlay (SR, BS);
        std::printf ("     total in %d out %d, buses:", proc.getTotalNumInputChannels(), proc.getTotalNumOutputChannels());
        for (int b = 0; b < proc.getBusCount (true); ++b)
            std::printf (" %s=%d", proc.getBus (true, b)->getName().toRawUTF8(), proc.getBus (true, b)->getNumberOfChannels());
        std::printf ("\n");
        check (proc.getLatencySamples() == 0, "instrument reports no latency (synths play instantly)");

        // choir from MIDI only
        auto s = run (proc, nullptr, 2.0, { 48, 55, 60, 64 }, 0);
        check (s.finite && s.rms > 0.01 && s.peak < 1.0f, "INIT choir chord plays: rms " + juce::String (s.rms, 3));

        // side-chain voice through the vocoder
        setParam (proc, proc, "voc_on", 1.0f);
        setParam (proc, proc, "choir_on", 0.0f);
        setParam (proc, proc, "voice_level", 0.0f);
        setParam (proc, proc, "tune_on", 0.0f);
        Singer singer;
        s = run (proc, &singer, 2.0, { 48, 55, 60 }, 0);
        check (s.finite && s.rms > 0.01, "side-chain voice drives the vocoder: rms " + juce::String (s.rms, 3));
        check (proc.readout.hasInput.load(), "editor sees the side-chain input");
        check (proc.readout.voiced.load() || proc.readout.inputMidi.load() >= 0.0f, "pitch read-out updates");

        // silence in = silence out from the vocoder (no MIDI-held drone without a voice)
        run (proc, nullptr, 3.0, {}, 0);                 // let the reverb / delay tails die away
        s = run (proc, nullptr, 1.0, { 48, 55, 60 }, 0);
        check (s.peak < 0.05f, "no voice -> vocoder stays quiet: peak " + juce::String (s.peak, 4));

        // every factory patch, with voice and chord
        const auto& factory = FactoryPatches::all();
        bool allOk = true;
        for (int i = 0; i < static_cast<int> (factory.size()); ++i)
        {
            proc.patches.load (i);
            Singer sg;
            const auto st = run (proc, &sg, 1.5, { 57, 60, 64 }, 0);
            const bool ok = st.finite && st.peak <= 1.0f && st.rms > 0.002;
            if (! ok) std::printf ("     patch %s: rms %.4f peak %.3f finite %d\n", factory[static_cast<size_t> (i)].name, st.rms, st.peak, st.finite ? 1 : 0);
            allOk = allOk && ok;
        }
        check (allOk, juce::String (static_cast<int> (factory.size())) + " factory patches all make sound, finite, within full scale");

        // state round trip
        proc.patches.load (4);   // HARD TUNE // TRAP
        setParam (proc, proc, "chg_pitch", 3.0f);
        juce::MemoryBlock state;
        proc.getStateInformation (state);
        VoicesAudioProcessor other;
        other.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        bool same = true;
        for (const auto& sp : Params::specs)
            same = same && std::abs (other.apvts.getRawParameterValue (sp.id)->load() - proc.apvts.getRawParameterValue (sp.id)->load()) < 1.0e-5f;
        check (same, "state save / restore keeps every parameter");
        check (other.patches.getCurrentName() == "HARD TUNE // TRAP" && other.patches.isModified(), "session remembers patch name + modified flag");

        // parameter text
        check (proc.apvts.getParameter ("tune_scale")->getText (proc.apvts.getParameter ("tune_scale")->convertTo0to1 (2.0f), 32) == "MINOR",
               "choice parameters show names (scale 2 = MINOR)");
        check (proc.apvts.getParameter ("vsyn_vowel")->getText (proc.apvts.getParameter ("vsyn_vowel")->convertTo0to1 (3.0f), 32) == "O",
               "vowel parameter shows the vowel (3 = O)");
        check (proc.apvts.getParameter ("tune_retune")->getText (0.0f, 32) == "0 ms", "retune 0 shows 0 ms");

        // sustain pedal holds the choir
        proc.patches.load (0);
        juce::AudioBuffer<float> buf (2, BS);
        juce::MidiBuffer m;
        m.addEvent (juce::MidiMessage::controllerEvent (1, 64, 127), 0);
        m.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 1);
        m.addEvent (juce::MidiMessage::noteOff (1, 60), 100);
        buf.clear(); proc.processBlock (buf, m);
        for (int b = 0; b < 100; ++b) { juce::MidiBuffer none; buf.clear(); proc.processBlock (buf, none); }
        check (buf.getMagnitude (0, 0, BS) > 0.01f, "sustain pedal keeps the note sounding after key-up");
        m.clear(); m.addEvent (juce::MidiMessage::controllerEvent (1, 64, 0), 0);
        buf.clear(); proc.processBlock (buf, m);
        for (int b = 0; b < 300; ++b) { juce::MidiBuffer none; buf.clear(); proc.processBlock (buf, none); }
        check (buf.getMagnitude (0, 0, BS) < 0.002f, "pedal up releases it");

        // other sample rates / huge blocks
        proc.prepareToPlay (96000.0, 2048);
        juce::AudioBuffer<float> big (2, 2048);
        bool ok = true;
        for (int b = 0; b < 50; ++b)
        {
            juce::MidiBuffer mm; if (b == 0) mm.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0);
            big.clear(); proc.processBlock (big, mm);
            ok = ok && std::isfinite (big.getMagnitude (0, 0, 2048));
        }
        check (ok, "96 kHz / 2048-sample blocks");
        proc.prepareToPlay (SR, BS);
    }

    //==========================================================================
    std::printf ("\n2. Editor screenshots (audio running)\n");
    {
        proc.patches.load (0);
        setParam (proc, proc, "tune_scale", 1.0f);
        setParam (proc, proc, "tune_retune", 0.0f);
        auto editor = std::unique_ptr<juce::AudioProcessorEditor> (proc.createEditorIfNeeded());
        editor->setSize (Theme::designWidth, Theme::designHeight);
        editor->setVisible (true);
        Singer singer;
        auto tick = [] { pump (11); };
        run (proc, &singer, 4.0, { 48, 55, 60, 64 }, 0, tick, false);
        check (savePng (*editor, shots.getChildFile ("main_view.png")), "main_view.png (hard tune + choir)");

        proc.patches.load (5);   // ROBOT CHOIR
        run (proc, &singer, 3.0, { 45, 52, 57, 60 }, 0, tick, false);
        check (savePng (*editor, shots.getChildFile ("vocoder_view.png")), "vocoder_view.png (ROBOT CHOIR)");

        proc.patches.load (8);   // ANGEL ARP
        setParam (proc, proc, "chg_on", 1.0f);
        run (proc, &singer, 3.0, { 57, 60, 64 }, 0, tick, false);
        check (savePng (*editor, shots.getChildFile ("arp_view.png")), "arp_view.png (ANGEL ARP)");

        editor->setSize (Theme::designWidth / 2, Theme::designHeight / 2);
        check (editor->getWidth() == Theme::designWidth / 2, "editor scales down to 50%");
        editor.reset();
    }

    //==========================================================================
    auto testVst3 = [] (const juce::String& path, bool expectInstrument)
    {
        std::printf ("\n3. VST3 host test: %s\n", path.toRawUTF8());
        juce::VST3PluginFormat format;
        juce::OwnedArray<juce::PluginDescription> types;
        format.findAllTypesForFile (types, path);
        check (types.size() == 1, "VST3 scanned");
        if (types.isEmpty()) return;
        const auto& d = *types[0];
        check (d.isInstrument == expectInstrument, juce::String ("category: ") + (d.isInstrument ? "instrument" : "effect") + " (" + d.category + ")");
        juce::String err;
        auto inst = format.createInstanceFromDescription (d, SR, BS, err);
        check (inst != nullptr, "instantiated " + d.name + " by " + d.manufacturerName + err);
        if (inst == nullptr) return;

        std::printf ("     buses in: %d (", inst->getBusCount (true));
        for (int b = 0; b < inst->getBusCount (true); ++b)
            std::printf ("%s%s:%s", b ? ", " : "", inst->getBus (true, b)->getName().toRawUTF8(),
                         inst->getBus (true, b)->isEnabled() ? "on" : "off");
        std::printf (")  latency %d\n", inst->getLatencySamples());

        inst->enableAllBuses();
        inst->setRateAndBufferSizeDetails (SR, BS);
        inst->prepareToPlay (SR, BS);
        Singer singer;
        // FX: voice on the main input, hard tune. Instrument: chord + side-chain voice.
        const int voiceOffset = 0;
        auto st = run (*inst, &singer, 2.0, { 48, 55, 60, 64 }, voiceOffset);
        check (st.finite && st.rms > 0.01 && st.peak <= 1.0f, "plays through the host: rms " + juce::String (st.rms, 3));
        if (! expectInstrument)
            check (inst->getLatencySamples() > 0 && inst->getLatencySamples() < 1000, "FX reports its latency: " + juce::String (inst->getLatencySamples()));

        juce::MemoryBlock state;
        inst->getStateInformation (state);
        inst->setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        check (state.getSize() > 100, "state chunk round trip (" + juce::String (static_cast<int> (state.getSize())) + " bytes)");

        if (auto* ed = inst->createEditorIfNeeded())
        {
            check (ed->getWidth() > 500, "editor opens in the host: " + juce::String (ed->getWidth()) + "x" + juce::String (ed->getHeight()));
            delete ed;
        }
        inst->releaseResources();
    };
    if (argc > 2) testVst3 (argv[2], true);
    if (argc > 3) testVst3 (argv[3], false);

    std::printf ("\n%s - %d passed, %d failed\n", failures == 0 ? "ALL PASSED" : "FAILURES", passes, failures);
    return failures == 0 ? 0 : 1;
}
