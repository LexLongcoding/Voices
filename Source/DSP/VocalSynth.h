// Voices - Copyright (C) 2026 LexLongcoding
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
#include "ChoirSynth.h"   // PolyVoices

// Solo singing voice with selectable vowel (A E I O U, morphable in between).
// Glottal-style source (band-limited pulse + saw, softened by a tilt filter)
// through five vowel formants, with delayed natural vibrato, a little pitch
// jitter, breath noise and glide between notes.
// MIXED = a duet: female voice on the note plus a male voice an octave below.
class VocalSynth
{
public:
    struct Settings
    {
        float vowel = 0.0f;     // 0..4  A E I O U
        float vibrato = 0.4f;
        float breath = 0.1f;
        float bright = 0.5f;
        float glide = 0.03f;    // seconds
        float attack = 0.04f, release = 0.35f;
        Formants::VoiceType type = Formants::VoiceType::female;
    };

    static constexpr int numVoices = 8;

    void prepare (double sr)
    {
        sampleRate = sr;
        uint32_t seed = 0x51A6u;
        for (auto& v : poly.voices) v.prepare (sr, seed += 104729u);
        updateFormants (true);
    }

    void setSettings (const Settings& s) { settings = s; }

    void noteOn (int note, float velocity)
    {
        auto& v = poly.allocate (note);
        v.start (note, velocity, poly.nextAge(), settings, lastNote);
        lastNote = note;
    }
    void noteOff (int note) { poly.noteOff (note); }
    void allNotesOff()      { poly.allOff(); }
    void reset()            { poly.kill(); lastNote = -1; }

    void render (float* l, float* r, int n)
    {
        int done = 0;
        while (done < n)
        {
            if (ctrlCountdown <= 0) { updateFormants (false); ctrlCountdown = 32; }
            const int todo = std::min (n - done, ctrlCountdown);
            for (auto& v : poly.voices)
                if (v.isActive())
                    v.render (l + done, r + done, todo, settings);
            ctrlCountdown -= todo;
            done += todo;
        }
    }

private:
    struct Layer
    {
        float phase = 0.0f;
        Formants::Bank bank;
        Dsp::OnePole tilt;
    };

    struct Voice
    {
        int note = -1;
        uint32_t age = 0;
        double sr = 48000.0;
        float freq = 220.0f, startFreq = 220.0f, glidePos = 1.0f, glideStep = 1.0f;
        float velGain = 1.0f, vibPhase = 0.0f, vibOnset = 0.0f, jitter = 0.0f, jitterTarget = 0.0f;
        int jitterCount = 0;
        Layer upper, lower;      // lower = male octave-down layer in MIXED
        Dsp::AREnvelope env;
        Dsp::Noise noise;

        void prepare (double sampleRate, uint32_t seed)
        {
            sr = sampleRate;
            noise.seed (seed);
            upper.bank.prepare (sampleRate);
            lower.bank.prepare (sampleRate);
        }
        bool isActive() const    { return env.isActive(); }
        bool isReleasing() const { return env.isReleasing(); }
        void release()           { env.noteOff(); }
        void kill()              { env.kill(); note = -1; }

        void start (int n, float velocity, uint32_t a, const Settings& s, int previousNote)
        {
            const bool wasActive = env.isActive();
            const float newFreq = Dsp::midiToHz (static_cast<float> (n));
            startFreq = (previousNote >= 0 && s.glide > 0.001f) ? Dsp::midiToHz (static_cast<float> (previousNote))
                                                                : newFreq;
            if (wasActive && note >= 0) startFreq = currentFreq();
            freq = newFreq;
            glidePos = std::abs (startFreq - newFreq) < 0.01f ? 1.0f : 0.0f;
            glideStep = 1.0f / std::max (1.0f, s.glide * static_cast<float> (sr));
            note = n; age = a;
            velGain = 0.5f + 0.5f * velocity;
            vibOnset = 0.0f;
            env.setTimes (s.attack, s.release, sr);
            env.noteOn();
            if (! wasActive)
            {
                upper.bank.reset(); lower.bank.reset();
                upper.phase = noise.uniform(); lower.phase = upper.phase;
                vibPhase = noise.uniform();
            }
        }

        float currentFreq() const
        {
            // exponential (musical) glide
            return startFreq * std::pow (freq / startFreq, std::min (1.0f, glidePos));
        }

        void render (float* outL, float* outR, int n, const Settings& s)
        {
            const float invSr = 1.0f / static_cast<float> (sr);
            const bool mixed = s.type == Formants::VoiceType::mixed;
            const float vibDepth = s.vibrato * 0.5f;            // semitones
            const float tiltHz = 1200.0f + s.bright * s.bright * 9000.0f;
            upper.tilt.setCutoff (tiltHz, sr);
            lower.tilt.setCutoff (tiltHz * 0.8f, sr);
            const float pulseMix = 0.35f + 0.4f * s.bright;
            const float gain = velGain * 0.38f * (mixed ? 0.72f : 1.0f);

            for (int i = 0; i < n; ++i)
            {
                const float e = env.next();
                if (e <= 0.0f && ! env.isActive()) { note = -1; break; }

                if (glidePos < 1.0f) glidePos += glideStep;
                vibOnset = std::min (1.0f, vibOnset + invSr * 2.5f);   // vibrato blooms in over ~0.4 s
                vibPhase += 5.4f * invSr; if (vibPhase >= 1.0f) vibPhase -= 1.0f;

                if (--jitterCount <= 0)   // slow random pitch wander: keeps the voice alive
                {
                    jitterTarget = noise.next() * 0.06f;
                    jitterCount = static_cast<int> (sr * 0.05);
                }
                jitter += (jitterTarget - jitter) * 0.0008f;

                const float bend = vibDepth * vibOnset * vibOnset * std::sin (Dsp::twoPi * vibPhase) + jitter;
                const float f = currentFreq() * (1.0f + bend * 0.05776f);

                const float breathNoise = noise.next() * s.breath;
                float y = renderLayer (upper, f, invSr, pulseMix, breathNoise);
                if (mixed)
                    y += renderLayer (lower, f * 0.5f, invSr, pulseMix, breathNoise) * 0.9f;

                const float g = e * gain;
                outL[i] += y * g;
                outR[i] += y * g;
            }
        }

        static float renderLayer (Layer& L, float f, float invSr, float pulseMix, float breathNoise)
        {
            const float dt = std::min (0.45f, f * invSr);
            L.phase += dt; if (L.phase >= 1.0f) L.phase -= 1.0f;
            // narrow pulse ~ glottal closure burst; saw adds body
            const float src = Dsp::blepSaw (L.phase, dt) * (1.0f - pulseMix)
                            + Dsp::blepPulse (L.phase, dt, 0.18f) * pulseMix;
            const float shaped = L.tilt.process (src) * 1.6f;
            // breath rides on the glottal cycle (louder while the folds are open)
            const float aspiration = breathNoise * (L.phase < 0.6f ? 1.4f : 0.5f) * 3.0f;
            return L.bank.process (shaped + aspiration);
        }
    };

    void updateFormants (bool force)
    {
        const bool femaleUpper = settings.type != Formants::VoiceType::male;
        const auto up = Formants::vowel (femaleUpper, settings.vowel);
        const auto lo = Formants::vowel (false, settings.vowel);
        const float k = force ? 0.0f : 0.9f;
        auto glideTo = [k] (Formants::Set& cur, const Formants::Set& t)
        {
            for (size_t i = 0; i < Formants::numFormants; ++i)
            {
                cur.freq[i] = t.freq[i] + (cur.freq[i] - t.freq[i]) * k;
                cur.bw[i]   = t.bw[i]   + (cur.bw[i]   - t.bw[i])   * k;
                cur.gain[i] = t.gain[i] + (cur.gain[i] - t.gain[i]) * k;
            }
        };
        glideTo (curUpper, up);
        glideTo (curLower, lo);
        for (auto& v : poly.voices)
            if (v.isActive() || force)
            {
                v.upper.bank.setTarget (curUpper, 1.0f, 1.15f);
                v.lower.bank.setTarget (curLower, 1.0f, 1.15f);
            }
    }

    double sampleRate = 48000.0;
    Settings settings;
    PolyVoices<Voice, numVoices> poly;
    Formants::Set curUpper {}, curLower {};
    int ctrlCountdown = 0, lastNote = -1;
};
