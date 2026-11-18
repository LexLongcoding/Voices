// Voices - Copyright (C) 2026 LexLongcoding
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
#include "DspUtils.h"
#include "Formants.h"
#include <array>
#include <atomic>

// Channel vocoder: your voice (modulator) shapes a synth (carrier) band by band.
// 8..32 bands, log-spaced 110 Hz .. 8 kHz, 4th-order band-pass filters on both sides.
// The carrier is a built-in polyphonic synth played from MIDI; with AUTO on and no
// keys held it follows the pitch you sing (the classic robot voice, no keyboard needed).
class Vocoder
{
public:
    enum class Carrier { saw = 0, pulse = 1, super = 2 };

    struct Settings
    {
        int bands = 20;
        Carrier carrier = Carrier::super;
        float release = 0.06f;      // envelope release, seconds
        float formantSemis = 0.0f;  // shifts the carrier filter bank
        float sibilance = 0.4f;
        Formants::VoiceType type = Formants::VoiceType::mixed;
    };

    static constexpr int maxBands = 32;
    static constexpr int numVoices = 8;

    void prepare (double sr)
    {
        sampleRate = sr;
        sibHp1.setCoefficients (4500.0f, 0.7f, sr);
        sibHp2.setCoefficients (4500.0f, 0.7f, sr);
        emphasis.setCoefficients (1200.0f, 0.7f, sr);
        attackCoeff = Dsp::smoothCoeff (0.002f, sr);
        builtBands = -1;
        rebuild();
        reset();
    }

    void reset()
    {
        for (auto& b : band) { b.m1.reset(); b.m2.reset(); b.c1.reset(); b.c2.reset(); b.env = 0.0f; }
        for (auto& v : voices) v.gate = v.level = 0.0f;
        sibHp1.reset(); sibHp2.reset(); emphasis.reset();
        autoActive = false;
    }

    void setSettings (const Settings& s)
    {
        const bool changed = s.bands != settings.bands || std::abs (s.formantSemis - settings.formantSemis) > 1.0e-4f || s.type != settings.type;
        settings = s;
        settings.bands = std::clamp (s.bands, 8, maxBands);
        releaseCoeff = Dsp::smoothCoeff (std::max (0.005f, s.release), sampleRate);
        if (changed) rebuild();
    }

    void noteOn (int note, float velocity)
    {
        Voice* v = nullptr;
        for (auto& x : voices) if (x.note == note && x.gate > 0.0f) v = &x;
        if (! v) for (auto& x : voices) if (x.gate <= 0.0f && x.level < 0.001f) { v = &x; break; }
        if (! v) { v = &voices[0]; for (auto& x : voices) if (x.age < v->age) v = &x; }
        v->note = note;
        v->freq = Dsp::midiToHz (static_cast<float> (note));
        v->gate = 0.6f + 0.4f * velocity;
        v->age = ++ageCounter;
        ++held;
    }
    void noteOff (int note)
    {
        for (auto& v : voices) if (v.note == note && v.gate > 0.0f) { v.gate = 0.0f; held = std::max (0, held - 1); }
    }
    void allNotesOff() { for (auto& v : voices) v.gate = 0.0f; held = 0; }

    // Auto mode: carrier follows this pitch (MIDI note, fractional) while no keys are held.
    void setAutoPitch (bool enabled, bool voiced, float midi)
    {
        autoEnabled = enabled;
        if (voiced) autoFreq = Dsp::midiToHz (midi);
        autoActive = enabled && held == 0;
    }

    // modulator: mono voice input. Adds stereo output.
    void render (const float* modulator, float* outL, float* outR, int n)
    {
        const int nb = settings.bands;
        const float invSr = 1.0f / static_cast<float> (sampleRate);
        const bool sub = settings.type != Formants::VoiceType::female;   // male / mixed add a sub-octave
        const bool superSaw = settings.carrier == Carrier::super;
        constexpr float superDetune[3] = { 1.0f, 1.0059f, 0.9942f };

        for (int i = 0; i < n; ++i)
        {
            // --- carrier
            float car = 0.0f;
            for (auto& v : voices)
            {
                const float target = v.gate;
                v.level += (target - v.level) * (target > v.level ? 0.01f : 0.0015f);
                if (v.level < 0.0005f && v.gate <= 0.0f) continue;
                car += oscillate (v, v.freq, invSr, sub, superSaw, superDetune) * v.level;
            }
            autoLevel += ((autoActive ? 1.0f : 0.0f) - autoLevel) * 0.002f;
            if (autoLevel > 0.0005f)
                car += oscillate (autoVoice, autoFreq, invSr, sub, superSaw, superDetune) * autoLevel;
            car += noise.next() * 0.06f;   // a little noise in the carrier keeps consonants intelligible

            // --- modulator, with pre-emphasis so the high bands get enough level
            const float m = modulator[i];
            const float me = m + emphasis.highpass (m) * 1.5f;

            float yl = 0.0f, yr = 0.0f;
            for (int b = 0; b < nb; ++b)
            {
                auto& bd = band[static_cast<size_t> (b)];
                const float mb = bd.m2.bandpass (bd.m1.bandpass (me));
                const float a = std::abs (mb);
                bd.env = a > bd.env ? a + (bd.env - a) * attackCoeff : a + (bd.env - a) * releaseCoeff;
                const float cb = bd.c2.bandpass (bd.c1.bandpass (car)) * bd.env;
                yl += cb * bd.panL;
                yr += cb * bd.panR;
            }

            // unvoiced consonants (s, t, sh) come straight from the voice
            const float sib = sibHp2.highpass (sibHp1.highpass (m)) * settings.sibilance * 1.2f;
            outL[i] += yl * gainComp + sib;
            outR[i] += yr * gainComp + sib;
        }

        for (int b = 0; b < nb; ++b)   // for the band display
            bandDisplay[static_cast<size_t> (b)].store (band[static_cast<size_t> (b)].env, std::memory_order_relaxed);
    }

    int   getNumBands() const { return settings.bands; }
    float getBandLevel (int b) const { return bandDisplay[static_cast<size_t> (std::clamp (b, 0, maxBands - 1))].load (std::memory_order_relaxed); }

private:
    struct Voice
    {
        int note = -1;
        uint32_t age = 0;
        float freq = 220.0f, gate = 0.0f, level = 0.0f;
        float ph[3] { 0.0f, 0.33f, 0.66f }, subPh = 0.0f;
    };

    float oscillate (Voice& v, float f, float invSr, bool sub, bool superSaw, const float* det)
    {
        float y = 0.0f;
        const int count = superSaw ? 3 : 1;
        for (int k = 0; k < count; ++k)
        {
            const float dt = std::min (0.45f, f * det[k] * invSr);
            v.ph[k] += dt; if (v.ph[k] >= 1.0f) v.ph[k] -= 1.0f;
            y += settings.carrier == Carrier::pulse ? Dsp::blepPulse (v.ph[k], dt, 0.5f) : Dsp::blepSaw (v.ph[k], dt);
        }
        if (superSaw) y *= 0.6f;
        if (sub)
        {
            const float dt = std::min (0.45f, f * 0.5f * invSr);
            v.subPh += dt; if (v.subPh >= 1.0f) v.subPh -= 1.0f;
            y += Dsp::blepSaw (v.subPh, dt) * 0.55f;
        }
        return y;
    }

    void rebuild()
    {
        const int nb = settings.bands;
        builtBands = nb;
        const float lo = 110.0f, hi = std::min (8000.0f, static_cast<float> (sampleRate) * 0.42f);
        const float octaves = std::log2 (hi / lo);
        const float bwOct = octaves / static_cast<float> (nb);
        const float q = 1.0f / (2.0f * std::sinh (0.5f * std::log (2.0f) * bwOct)) * 1.1f;
        float shift = Dsp::semisToRatio (settings.formantSemis);
        if (settings.type == Formants::VoiceType::female) shift *= Dsp::semisToRatio (2.0f);
        if (settings.type == Formants::VoiceType::male)   shift *= Dsp::semisToRatio (-2.0f);
        for (int b = 0; b < nb; ++b)
        {
            auto& bd = band[static_cast<size_t> (b)];
            const float f = lo * std::exp2 (bwOct * (static_cast<float> (b) + 0.5f));
            bd.m1.setCoefficients (f, q, sampleRate);
            bd.m2.setCoefficients (f, q, sampleRate);
            bd.c1.setCoefficients (f * shift, q, sampleRate);
            bd.c2.setCoefficients (f * shift, q, sampleRate);
            // gentle stereo: bands alternate a little left / right
            Dsp::panGains ((b % 2 ? 0.35f : -0.35f), bd.panL, bd.panR);
        }
        // more, narrower bands pass less energy each: compensate
        gainComp = 2.6f * std::sqrt (static_cast<float> (nb) / 20.0f) * 1.8f;
    }

    struct Band
    {
        Dsp::Svf m1, m2, c1, c2;
        float env = 0.0f, panL = 0.7f, panR = 0.7f;
    };

    double sampleRate = 48000.0;
    Settings settings;
    std::array<Band, maxBands> band {};
    std::array<std::atomic<float>, maxBands> bandDisplay {};   // read by the editor
    std::array<Voice, numVoices> voices {};
    Voice autoVoice;
    Dsp::Svf sibHp1, sibHp2, emphasis;
    Dsp::Noise noise { 0xB0C0DEu };
    float attackCoeff = 0.0f, releaseCoeff = 0.99f, gainComp = 1.0f;
    float autoFreq = 220.0f, autoLevel = 0.0f;
    bool autoEnabled = true, autoActive = false;
    int held = 0, builtBands = -1;
    uint32_t ageCounter = 0;
};
