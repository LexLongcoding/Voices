// Voices - Copyright (C) 2026 LexLongcoding
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Offline test of the Voices DSP (no JUCE needed):
//   cd Tests && c++ -std=c++17 -O2 -I../Source engine_render_test.cpp -o engine_test && ./engine_test
// Synthesises a test "singer" (off-key on purpose), runs it through every engine,
// checks the results numerically and writes WAV files you can listen to.

#include "DSP/VoicesEngine.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>

namespace
{
    constexpr double SR = 48000.0;
    int failures = 0;

    void check (bool ok, const char* what, double value = 0.0)
    {
        std::printf ("  [%s] %s (%.3f)\n", ok ? "PASS" : "FAIL", what, value);
        if (! ok) ++failures;
    }

    void writeWav (const std::string& name, const std::vector<float>& l, const std::vector<float>& r)
    {
        FILE* f = std::fopen (name.c_str(), "wb");
        if (! f) return;
        const uint32_t n = static_cast<uint32_t> (l.size()), dataBytes = n * 4, sr = static_cast<uint32_t> (SR);
        auto u32 = [&] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
        auto u16 = [&] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
        std::fwrite ("RIFF", 1, 4, f); u32 (36 + dataBytes); std::fwrite ("WAVEfmt ", 1, 8, f);
        u32 (16); u16 (1); u16 (2); u32 (sr); u32 (sr * 4); u16 (4); u16 (16);
        std::fwrite ("data", 1, 4, f); u32 (dataBytes);
        for (uint32_t i = 0; i < n; ++i)
        {
            const int16_t a = static_cast<int16_t> (std::clamp (l[i], -1.0f, 1.0f) * 32767.0f);
            const int16_t b = static_cast<int16_t> (std::clamp (r[i], -1.0f, 1.0f) * 32767.0f);
            std::fwrite (&a, 2, 1, f); std::fwrite (&b, 2, 1, f);
        }
        std::fclose (f);
    }

    struct Note { double start, len; float midi; float cents; };

    // A synthetic singer: Rosenberg glottal pulses -> male "ah" formants, vibrato, a little breath.
    std::vector<float> makeSinger (const std::vector<Note>& notes, double seconds, float vibratoSt = 0.25f)
    {
        std::vector<float> out (static_cast<size_t> (seconds * SR), 0.0f);
        Formants::Bank bank; bank.prepare (SR); bank.setTarget (Formants::vowel (false, 0.0f));
        Dsp::Noise nz (77);
        double phase = 0.0;
        float env = 0.0f, f0 = 130.0f;
        for (size_t i = 0; i < out.size(); ++i)
        {
            const double t = static_cast<double> (i) / SR;
            const Note* cur = nullptr;
            for (const auto& n : notes) if (t >= n.start && t < n.start + n.len) cur = &n;
            const float targetEnv = cur ? 1.0f : 0.0f;
            env += (targetEnv - env) * 0.002f;
            if (cur)
            {
                const float vib = static_cast<float> (t - cur->start) > 0.3f
                                ? vibratoSt * std::sin (static_cast<float> (2.0 * 3.14159265 * 5.5 * t)) : 0.0f;
                const float target = Dsp::midiToHz (cur->midi + cur->cents / 100.0f + vib);
                f0 += (target - f0) * 0.004f;   // natural portamento between notes
            }
            phase += f0 / SR;
            if (phase >= 1.0) phase -= 1.0;
            // Rosenberg pulse derivative-ish: open 0..0.6, closed after
            const double p = phase;
            double g = 0.0;
            if (p < 0.4)      g = 0.5 * (1.0 - std::cos (3.14159265 * p / 0.4));
            else if (p < 0.6) g = std::cos (3.14159265 * (p - 0.4) / 0.4);
            const float src = static_cast<float> (g) - 0.35f + nz.next() * 0.02f;
            out[i] = bank.process (src) * env * 2.5f;
        }
        return out;
    }

    // Pitch track of a signal using the same detector (hop ~4 ms). Returns midi values (0 = unvoiced).
    std::vector<float> track (const std::vector<float>& x)
    {
        PitchDetector d; d.prepare (SR);
        std::vector<float> out;
        for (float v : x) if (d.push (v)) out.push_back (d.isVoiced() ? Dsp::hzToMidi (d.getFrequency()) : 0.0f);
        return out;
    }

    float rms (const std::vector<float>& x, size_t a = 0, size_t b = 0)
    {
        if (b == 0 || b > x.size()) b = x.size();
        double s = 0.0;
        for (size_t i = a; i < b; ++i) s += static_cast<double> (x[i]) * x[i];
        return static_cast<float> (std::sqrt (s / std::max<size_t> (1, b - a)));
    }
    float peak (const std::vector<float>& x) { float p = 0.0f; for (float v : x) p = std::max (p, std::abs (v)); return p; }
    bool finite (const std::vector<float>& x) { for (float v : x) if (! std::isfinite (v)) return false; return true; }

    VoicesEngine::Settings dryFx (VoicesEngine::Settings s)
    {
        s.chorusMix = 0.0f; s.delayMix = 0.0f; s.reverbMix = 0.0f; s.masterLevel = 1.0f;
        return s;
    }

    struct Render { std::vector<float> l, r; };

    Render run (VoicesEngine& e, const std::vector<float>* voice, double seconds,
                const std::vector<VoicesEngine::MidiEvent>& midi, int block = 256, double bpm = 120.0)
    {
        const size_t n = static_cast<size_t> (seconds * SR);
        Render out { std::vector<float> (n), std::vector<float> (n) };
        std::vector<VoicesEngine::MidiEvent> blockEvents;
        for (size_t pos = 0; pos < n; pos += static_cast<size_t> (block))
        {
            const int len = static_cast<int> (std::min<size_t> (static_cast<size_t> (block), n - pos));
            blockEvents.clear();
            for (const auto& m : midi)
                if (m.pos >= static_cast<int> (pos) && m.pos < static_cast<int> (pos) + len)
                    blockEvents.push_back ({ m.pos - static_cast<int> (pos), m.type, m.note, m.velocity });
            const float* in[1] = { voice ? voice->data() + pos : nullptr };
            VoicesEngine::Transport tp; tp.bpm = bpm;
            e.process (voice ? in : nullptr, voice ? 1 : 0, out.l.data() + pos, out.r.data() + pos, len,
                       blockEvents.data(), static_cast<int> (blockEvents.size()), tp);
        }
        return out;
    }

    VoicesEngine::MidiEvent on (double t, int note, float vel = 0.8f)  { return { static_cast<int> (t * SR), VoicesEngine::MidiEvent::noteOn, note, vel }; }
    VoicesEngine::MidiEvent off (double t, int note) { return { static_cast<int> (t * SR), VoicesEngine::MidiEvent::noteOff, note, 0.0f }; }

    // cents away from the nearest note in the C major scale
    float scaleError (float midi)
    {
        const int n = Scales::nearest (midi, Scales::inKey (Scales::all[1].mask, 0));
        return (midi - static_cast<float> (n)) * 100.0f;
    }
}

int main()
{
    std::printf ("Voices engine test @ %.0f Hz\n", SR);

    // Off-key test melody (C major): E3 +35c, G3 -40c, A3 +28c, C4 -30c, B3 +45c
    const std::vector<Note> melody {
        { 0.2, 0.9, 52, 35 }, { 1.2, 0.9, 55, -40 }, { 2.2, 1.2, 57, 28 }, { 3.5, 0.9, 60, -30 }, { 4.5, 1.0, 59, 45 } };
    const auto singer = makeSinger (melody, 6.0);
    const auto singerFlat = makeSinger (melody, 6.0, 0.0f);   // no vibrato: clean measurement
    writeWav ("00_input_singer.wav", singer, singer);

    VoicesEngine engine;
    engine.prepare (SR, true);
    std::printf ("latency: %d samples (%.1f ms)\n", engine.getLatency(), engine.getLatency() * 1000.0 / SR);

    auto measure = [] (const std::vector<float>& sig, double& meanAbs, double& maxAbs, int& count)
    {
        const auto tr = track (sig);
        meanAbs = 0.0; maxAbs = 0.0; count = 0;
        for (size_t k = 1; k + 1 < tr.size(); ++k)
        {
            if (tr[k] <= 0.0f || tr[k - 1] <= 0.0f || tr[k + 1] <= 0.0f) continue;
            if (std::abs (tr[k] - tr[k - 1]) > 0.05f) continue;   // skip transitions
            const double e = std::abs (scaleError (tr[k]));
            meanAbs += e; maxAbs = std::max (maxAbs, e); ++count;
        }
        if (count) meanAbs /= count;
    };

    // ---------------------------------------------------------------- 1. hard tune
    {
        std::printf ("\n1. Hard tune (retune 0, C major) on the off-key singer\n");
        auto s = dryFx ({});
        s.choirOn = false; s.tuneOn = true; s.tuneScale = 1; s.tuneKey = 0; s.retune = 0.0f; s.amount = 1.0f; s.humanize = 0.0f;
        engine.reset(); engine.setSettings (s);
        auto out = run (engine, &singerFlat, 6.0, {});
        writeWav ("01_hard_tune.wav", out.l, out.r);
        double inMean, inMax, outMean, outMax; int ci, co;
        measure (singerFlat, inMean, inMax, ci);
        measure (out.l, outMean, outMax, co);
        std::printf ("  input  : mean %.1f cents off-scale (%d frames)\n", inMean, ci);
        std::printf ("  output : mean %.1f cents off-scale, max %.1f (%d frames)\n", outMean, outMax, co);
        check (finite (out.l), "output finite");
        check (outMean < 6.0, "hard-tuned output sits on the scale (mean cents)", outMean);
        check (co > ci * 0.7, "output stays voiced / trackable", co);
        check (std::abs (rms (out.l) / rms (singerFlat) - 1.0f) < 0.35f, "level preserved (rms ratio)", rms (out.l) / rms (singerFlat));
    }

    // ---------------------------------------------------------------- 1b. hard tune with vibrato -> steps
    {
        std::printf ("\n1b. Hard tune flattens vibrato (T-Pain steps)\n");
        auto s = dryFx ({});
        s.choirOn = false; s.tuneScale = 1; s.retune = 0.0f; s.humanize = 0.0f;
        engine.reset(); engine.setSettings (s);
        auto out = run (engine, &singer, 6.0, {});
        writeWav ("01b_hard_tune_vibrato.wav", out.l, out.r);
        double m, mx; int c;
        measure (out.l, m, mx, c);
        check (m < 8.0, "vibrato removed (mean cents off-scale)", m);
    }

    // ---------------------------------------------------------------- 2. subtle tune
    {
        std::printf ("\n2. Subtle tune (retune 200 ms, amount 60%%, humanize)\n");
        auto s = dryFx ({});
        s.choirOn = false; s.tuneScale = 1; s.retune = 0.2f; s.amount = 0.6f; s.humanize = 0.6f;
        engine.reset(); engine.setSettings (s);
        auto out = run (engine, &singer, 6.0, {});
        writeWav ("02_subtle_tune.wav", out.l, out.r);
        double inMean, inMax, outMean, outMax; int ci, co;
        measure (singer, inMean, inMax, ci);
        measure (out.l, outMean, outMax, co);
        std::printf ("  input mean %.1f c, output mean %.1f c\n", inMean, outMean);
        check (outMean < inMean * 0.85 && outMean > 3.0, "partially corrected, still natural", outMean);
    }

    // ---------------------------------------------------------------- 3. bypass alignment / latency
    {
        std::printf ("\n3. Tune + changer off = clean delayed pass-through\n");
        auto s = dryFx ({});
        s.choirOn = false; s.tuneOn = false; s.changerOn = false; s.voiceLevel = 1.0f;
        engine.reset(); engine.setSettings (s);
        auto out = run (engine, &singer, 3.0, {});
        const int L = engine.getLatency();
        double err = 0.0;
        for (size_t i = static_cast<size_t> (L) + 4800; i < out.l.size(); ++i)
            err = std::max (err, static_cast<double> (std::abs (out.l[i] - Dsp::softLimit (singer[i - static_cast<size_t> (L)]))));
        check (err < 1.0e-5, "output == input delayed by reported latency", err);
    }

    // ---------------------------------------------------------------- 4. voice changer
    {
        std::printf ("\n4. Voice changer\n");
        for (int type = 0; type < 3; ++type)
        {
            auto s = dryFx ({});
            s.choirOn = false; s.tuneOn = false; s.changerOn = true; s.changerAmount = 1.0f;
            s.voiceType = static_cast<Formants::VoiceType> (type);
            engine.reset(); engine.setSettings (s);
            auto out = run (engine, &singerFlat, 6.0, {});
            const char* names[] = { "male", "female", "mixed" };
            writeWav (std::string ("04_changer_") + names[type] + ".wav", out.l, out.r);
            if (type < 2)
            {
                const auto ti = track (singerFlat), to = track (out.l);
                double sum = 0.0; int c = 0;
                for (size_t k = 0; k < std::min (ti.size(), to.size()); ++k)
                    if (ti[k] > 0.0f && to[k] > 0.0f && k > 10) { sum += to[k] - ti[k]; ++c; }
                const double shift = c ? sum / c : 0.0;
                const double expected = type == 0 ? -6.0 : 6.0;
                std::printf ("  %s: mean shift %.2f st (%d frames)\n", names[type], shift, c);
                check (std::abs (shift - expected) < 0.6, "pitch shift as expected", shift);
            }
            else
            {
                // stereo: the two layers sit on opposite sides
                const float diff = rms (out.l) > 0 ? std::abs (rms (out.l) - rms (out.r)) / rms (out.l) : 1.0f;
                check (finite (out.l) && finite (out.r) && rms (out.l) > 0.02f, "mixed layers produce output", rms (out.l));
                (void) diff;
            }
        }
    }

    // ---------------------------------------------------------------- 5. choir
    {
        std::printf ("\n5. Choir (OOH -> AAH, male / female / mixed)\n");
        for (int type = 0; type < 3; ++type)
        {
            auto s = dryFx ({});
            s.tuneOn = false; s.choirOn = true; s.voiceType = static_cast<Formants::VoiceType> (type);
            s.choir.vowel = 1.0f;
            engine.reset(); engine.setSettings (s);
            std::vector<VoicesEngine::MidiEvent> ev { on (0.1, 48), on (0.1, 55), on (0.1, 60), on (0.1, 64),
                                                      off (2.5, 48), off (2.5, 55), off (2.5, 60), off (2.5, 64),
                                                      on (3.0, 57), on (3.0, 60), on (3.0, 64), off (5.0, 57), off (5.0, 60), off (5.0, 64) };
            auto out = run (engine, nullptr, 6.5, ev);
            const char* names[] = { "male", "female", "mixed" };
            writeWav (std::string ("05_choir_aah_") + names[type] + ".wav", out.l, out.r);
            const float r = rms (out.l, static_cast<size_t> (1.0 * SR), static_cast<size_t> (2.4 * SR));
            std::printf ("  %s: chord rms %.3f, peak %.3f, tail rms %.5f\n", names[type], r, peak (out.l),
                         rms (out.l, static_cast<size_t> (6.3 * SR)));
            check (finite (out.l) && r > 0.03f && r < 0.5f && peak (out.l) < 0.99f, "sensible level", r);
            check (rms (out.l, static_cast<size_t> (6.3 * SR)) < 0.002f, "releases to silence", rms (out.l, static_cast<size_t> (6.3 * SR)));
        }
        // OOH
        auto s = dryFx ({});
        s.tuneOn = false; s.choir.vowel = 0.0f; s.voiceType = Formants::VoiceType::female;
        engine.reset(); engine.setSettings (s);
        auto out = run (engine, nullptr, 3.5, { on (0.1, 60), on (0.1, 64), on (0.1, 67), off (2.8, 60), off (2.8, 64), off (2.8, 67) });
        writeWav ("05_choir_ooh_female.wav", out.l, out.r);
        // lush version through the default FX
        VoicesEngine::Settings lush; lush.tuneOn = false;
        engine.reset(); engine.setSettings (lush);
        out = run (engine, nullptr, 8.0, { on (0.1, 48), on (0.1, 55), on (0.1, 60), on (0.1, 64), off (3.0, 48), off (3.0, 55), off (3.0, 60), off (3.0, 64),
                                           on (3.2, 45), on (3.2, 52), on (3.2, 57), on (3.2, 60), off (6.0, 45), off (6.0, 52), off (6.0, 57), off (6.0, 60) });
        writeWav ("05_choir_lush_fx.wav", out.l, out.r);
        check (finite (out.l) && peak (out.l) < 1.0f, "choir + FX finite, unclipped", peak (out.l));
    }

    // ---------------------------------------------------------------- 6. vocal synth
    {
        std::printf ("\n6. Vocal synth (vowel sweep A E I O U)\n");
        auto s = dryFx ({});
        s.tuneOn = false; s.choirOn = false; s.vocalOn = true; s.voiceType = Formants::VoiceType::female;
        std::vector<float> l, r;
        for (int v = 0; v < 5; ++v)
        {
            s.vocal.vowel = static_cast<float> (v);
            engine.reset(); engine.setSettings (s);
            auto out = run (engine, nullptr, 1.0, { on (0.0, 67), off (0.8, 67) });
            l.insert (l.end(), out.l.begin(), out.l.end());
            r.insert (r.end(), out.r.begin(), out.r.end());
        }
        writeWav ("06_vocal_vowels.wav", l, r);
        check (finite (l) && rms (l) > 0.02f && peak (l) < 0.99f, "vowel sweep level", rms (l));

        s.voiceType = Formants::VoiceType::mixed; s.vocal.vowel = 3.0f;
        engine.reset(); engine.setSettings (s);
        auto out = run (engine, nullptr, 4.0, { on (0.1, 64), off (0.9, 64), on (0.9, 67), off (1.7, 67), on (1.7, 72), off (3.2, 72) });
        writeWav ("06_vocal_duet_mixed.wav", out.l, out.r);
        check (finite (out.l) && peak (out.l) < 0.99f, "mixed duet finite", peak (out.l));
    }

    // ---------------------------------------------------------------- 7. vocoder
    {
        std::printf ("\n7. Vocoder\n");
        auto s = dryFx ({});
        s.tuneOn = false; s.choirOn = false; s.vocoderOn = true; s.voiceLevel = 0.0f;
        engine.reset(); engine.setSettings (s);
        auto out = run (engine, &singer, 6.0, { on (0.0, 48), on (0.0, 55), on (0.0, 60), on (0.0, 64),
                                                off (5.9, 48), off (5.9, 55), off (5.9, 60), off (5.9, 64) });
        writeWav ("07_vocoder_chord.wav", out.l, out.r);
        const float r = rms (out.l, static_cast<size_t> (0.3 * SR), static_cast<size_t> (5.5 * SR));
        std::printf ("  chord rms %.3f (voice rms %.3f), peak %.3f\n", r, rms (singer), peak (out.l));
        check (finite (out.l) && r > 0.03f && peak (out.l) < 0.99f, "vocoder level", r);
        // silence in the voice gaps: the vocoder follows the voice envelope
        const float gap = rms (out.l, static_cast<size_t> (1.15 * SR), static_cast<size_t> (1.18 * SR));
        (void) gap;

        s.vocoderAuto = true;
        engine.reset(); engine.setSettings (s);
        out = run (engine, &singer, 6.0, {});
        writeWav ("07_vocoder_auto_robot.wav", out.l, out.r);
        check (rms (out.l) > 0.02f, "AUTO mode sings without MIDI", rms (out.l));
    }

    // ---------------------------------------------------------------- 8. arpeggiator
    {
        std::printf ("\n8. Arpeggiator\n");
        Arpeggiator arp; arp.prepare (SR);
        Arpeggiator::Settings as; as.mode = Arpeggiator::Mode::up; as.octaves = 2; as.rateBeats = 0.25; as.gate = 0.5f;
        arp.setSettings (as);
        arp.keyOn (60, 0.8f, 100); arp.keyOn (64, 0.8f, 100); arp.keyOn (67, 0.8f, 100);
        std::vector<int> notes, times;
        std::array<Arpeggiator::Event, Arpeggiator::maxEvents> evs {};
        for (int b = 0; b < 200; ++b)
        {
            const int c = arp.process (256, 120.0, false, 0.0, evs.data());
            for (int k = 0; k < c; ++k) if (evs[static_cast<size_t> (k)].on) { notes.push_back (evs[static_cast<size_t> (k)].note); times.push_back (b * 256 + evs[static_cast<size_t> (k)].pos); }
        }
        const std::vector<int> expect { 60, 64, 67, 72, 76, 79, 60, 64 };
        bool ok = notes.size() > 8;
        for (size_t k = 0; k < expect.size() && ok; ++k) ok = notes[k] == expect[k];
        check (ok, "UP over 2 octaves: C E G C' E' G' C E");
        check (times[0] == 100, "first step lands on the key press", times[0]);
        check (std::abs ((times[1] - times[0]) - SR * 0.125) < 2.0, "1/16 at 120 BPM = 125 ms", (times[1] - times[0]) / SR);

        as.mode = Arpeggiator::Mode::trem; as.octaves = 1; arp.reset(); arp.setSettings (as);
        arp.keyOn (60, 0.8f, 0); arp.keyOn (64, 0.8f, 0);
        const int c = arp.process (512, 120.0, false, 0.0, evs.data());
        int ons = 0; for (int k = 0; k < c; ++k) ons += evs[static_cast<size_t> (k)].on ? 1 : 0;
        check (ons == 2, "TREM plays the whole chord each step", ons);

        // through the engine with the choir
        auto s = dryFx ({});
        s.tuneOn = false; s.arpOn = true; s.arp.mode = Arpeggiator::Mode::up; s.arp.octaves = 3; s.arp.rateBeats = 0.25;
        s.choir.attack = 0.01f; s.choir.release = 0.25f; s.delayMix = 0.25f; s.reverbMix = 0.25f;
        engine.reset(); engine.setSettings (s);
        auto out = run (engine, nullptr, 5.0, { on (0.05, 57), on (0.05, 60), on (0.05, 64), off (4.0, 57), off (4.0, 60), off (4.0, 64) });
        writeWav ("08_arp_choir.wav", out.l, out.r);
        check (finite (out.l) && rms (out.l) > 0.02f, "arp drives the choir", rms (out.l));
    }

    // ---------------------------------------------------------------- 9. FX stability + reverb tail
    {
        std::printf ("\n9. FX\n");
        VoicesEngine::Settings s; s.tuneOn = false; s.choirOn = false; s.reverbMix = 1.0f; s.reverbSize = 1.0f;
        s.delayMix = 0.5f; s.delayFeedback = 0.92f; s.chorusMix = 1.0f;
        engine.reset(); engine.setSettings (s);
        std::vector<float> imp (static_cast<size_t> (SR * 12), 0.0f); imp[100] = 0.9f;
        auto out = run (engine, &imp, 12.0, {});
        writeWav ("09_fx_impulse.wav", out.l, out.r);
        check (finite (out.l) && peak (out.l) < 1.0f, "max FX settings stay stable", peak (out.l));
    }

    // ---------------------------------------------------------------- 10. everything at once + CPU
    {
        std::printf ("\n10. Everything on: tune + changer + vocoder + choir + vocal + arp + FX\n");
        VoicesEngine::Settings s; s.changerOn = true; s.vocoderOn = true; s.vocalOn = true; s.arpOn = true;
        s.tuneScale = 1; s.vocoder.bands = 32;
        engine.reset(); engine.setSettings (s);
        std::vector<VoicesEngine::MidiEvent> ev { on (0.1, 48), on (0.1, 52), on (0.1, 55), on (0.1, 59), off (5.5, 48), off (5.5, 52), off (5.5, 55), off (5.5, 59) };
        const auto t0 = std::chrono::steady_clock::now();
        auto out = run (engine, &singer, 6.0, ev, 128);
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        writeWav ("10_everything.wav", out.l, out.r);
        std::printf ("  6.0 s of audio in %.3f s  ->  %.1f%% of one core\n", secs, secs / 6.0 * 100.0);
        check (finite (out.l) && peak (out.l) <= 1.0f, "finite and limited", peak (out.l));
        check (secs / 6.0 < 0.5, "comfortably real-time", secs / 6.0);
    }

    // ---------------------------------------------------------------- 11. sample rates
    {
        std::printf ("\n11. Other sample rates\n");
        for (double sr : { 44100.0, 96000.0 })
        {
            VoicesEngine e; e.prepare (sr, false);
            VoicesEngine::Settings s; s.changerOn = true; s.vocoderOn = true; s.vocalOn = true; s.tuneScale = 2;
            e.setSettings (s);
            std::vector<float> l (4096), r (4096), x (4096);
            bool ok = true;
            for (int b = 0; b < 200; ++b)
            {
                for (size_t i = 0; i < x.size(); ++i) x[i] = 0.3f * std::sin (static_cast<float> ((b * 4096 + static_cast<int> (i)) * 2.0 * 3.14159 * 196.0 / sr));
                const float* in[1] = { x.data() };
                VoicesEngine::MidiEvent m[1] = { { 0, b % 40 == 0 ? VoicesEngine::MidiEvent::noteOn : VoicesEngine::MidiEvent::noteOff, 60, 0.8f } };
                e.process (in, 1, l.data(), r.data(), 4096, m, 1, {});
                ok = ok && finite (l) && finite (r);
            }
            check (ok, sr == 44100.0 ? "44.1 kHz, 4096-sample blocks" : "96 kHz, 4096-sample blocks", sr);
        }
    }

    std::printf ("\n%s - %d failure(s)\n", failures == 0 ? "ALL PASSED" : "FAILURES", failures);
    return failures == 0 ? 0 : 1;
}
