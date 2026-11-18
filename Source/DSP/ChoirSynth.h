// Voices - Copyright (C) 2026 LexLongcoding
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
#include "Formants.h"
#include <array>

// Voice allocation shared by the polyphonic engines: oldest-note stealing,
// retriggering a held note reuses its voice.
template <typename VoiceT, size_t N>
struct PolyVoices
{
    std::array<VoiceT, N> voices {};
    uint32_t counter = 0;

    VoiceT& allocate (int note)
    {
        for (auto& v : voices) if (v.isActive() && v.note == note) return v;
        for (auto& v : voices) if (! v.isActive()) return v;
        VoiceT* oldest = &voices[0];
        for (auto& v : voices)   // prefer stealing a released voice
            if ((v.isReleasing() && ! oldest->isReleasing()) || (v.isReleasing() == oldest->isReleasing() && v.age < oldest->age))
                oldest = &v;
        return *oldest;
    }
    void noteOff (int note) { for (auto& v : voices) if (v.note == note && v.isActive() && ! v.isReleasing()) v.release(); }
    void allOff()           { for (auto& v : voices) v.release(); }
    void kill()             { for (auto& v : voices) v.kill(); }
    uint32_t nextAge()      { return ++counter; }
};

// Classic keyboard "Choir Aahs / Voice Oohs", synthesised: every note is a small
// ensemble of singers (detuned, drifting, each with its own vibrato and place in
// the stereo field) sung through vowel formants, with breath noise on top.
// MIXED voices split each note between a male and a female section.
class ChoirSynth
{
public:
    struct Settings
    {
        float vowel = 1.0f;     // 0 OOH .. 1 AAH
        float size = 0.6f;      // ensemble spread
        float breath = 0.15f;
        float vibrato = 0.3f;
        float attack = 0.25f, release = 0.8f;
        Formants::VoiceType type = Formants::VoiceType::mixed;
    };

    static constexpr int numVoices = 8;
    static constexpr int numSingers = 8;

    void prepare (double sr)
    {
        sampleRate = sr;
        uint32_t seed = 0xC401Au;
        for (auto& v : poly.voices) v.prepare (sr, seed += 7919u);
        updateFormants (true);
    }

    void setSettings (const Settings& s) { settings = s; }

    void noteOn (int note, float velocity)
    {
        auto& v = poly.allocate (note);
        v.start (note, velocity, poly.nextAge(), settings);
    }
    void noteOff (int note) { poly.noteOff (note); }
    void allNotesOff()      { poly.allOff(); }
    void reset()            { poly.kill(); }

    // Adds into the stereo buffers.
    void render (float* l, float* r, int n)
    {
        int done = 0;
        while (done < n)
        {
            if (ctrlCountdown <= 0) { updateFormants (false); ctrlCountdown = ctrlInterval; }
            const int todo = std::min (n - done, ctrlCountdown);
            for (auto& v : poly.voices)
                if (v.isActive())
                    v.render (l + done, r + done, todo, settings);
            ctrlCountdown -= todo;
            done += todo;
        }
    }

    bool anyActive() const { for (auto& v : poly.voices) if (v.isActive()) return true; return false; }

private:
    struct Singer
    {
        float phase = 0.0f, detune = 1.0f, driftPhase = 0.0f, driftRate = 0.2f, driftDepth = 0.0f;
        float vibPhase = 0.0f, vibRate = 5.2f, panL = 0.7f, panR = 0.7f;
        bool male = false;
    };

    struct Voice
    {
        int note = -1;
        uint32_t age = 0;
        float freq = 220.0f, velGain = 1.0f;
        std::array<Singer, numSingers> singers {};
        Formants::Bank maleL, maleR, femL, femR;
        Dsp::AREnvelope env;
        Dsp::Noise noise;
        Dsp::OnePole tilt;
        double sr = 48000.0;
        float vibOnset = 0.0f;

        void prepare (double sampleRate, uint32_t seed)
        {
            sr = sampleRate;
            noise.seed (seed);
            for (auto* b : { &maleL, &maleR, &femL, &femR }) b->prepare (sampleRate);
            tilt.setCutoff (5200.0f, sampleRate);
        }

        bool isActive() const    { return env.isActive(); }
        bool isReleasing() const { return env.isReleasing(); }
        void release()           { env.noteOff(); }
        void kill()              { env.kill(); note = -1; }

        void start (int n, float velocity, uint32_t a, const Settings& s)
        {
            const bool wasActive = env.isActive();
            note = n; age = a;
            freq = Dsp::midiToHz (static_cast<float> (n));
            velGain = 0.55f + 0.45f * velocity;
            vibOnset = 0.0f;
            env.setTimes (s.attack, s.release, sr);
            env.noteOn();
            if (! wasActive)
                for (auto* b : { &maleL, &maleR, &femL, &femR }) b->reset();

            // a fresh random ensemble for every note: no two chords sound identical
            for (int i = 0; i < numSingers; ++i)
            {
                auto& sg = singers[static_cast<size_t> (i)];
                const float spread = 4.0f + s.size * 22.0f;                 // cents
                const float slot = (static_cast<float> (i) + 0.5f) / numSingers * 2.0f - 1.0f;
                sg.detune = Dsp::semisToRatio ((slot * 0.7f + noise.next() * 0.3f) * spread / 100.0f);
                if (! wasActive) sg.phase = noise.uniform();
                sg.driftPhase = noise.uniform();
                sg.driftRate = 0.12f + 0.35f * noise.uniform();
                sg.driftDepth = (2.0f + 6.0f * s.size) / 100.0f;            // semitones
                sg.vibPhase = noise.uniform();
                sg.vibRate = 4.8f + 0.9f * noise.uniform();
                const float pan = std::clamp ((i % 2 ? 1.0f : -1.0f) * (0.25f + 0.75f * std::abs (slot)) * (0.35f + 0.65f * s.size), -1.0f, 1.0f);
                Dsp::panGains (pan, sg.panL, sg.panR);
                sg.male = s.type == Formants::VoiceType::male || (s.type == Formants::VoiceType::mixed && (i % 4) < 2);
            }
        }

        void setFormants (const Formants::Set& male, const Formants::Set& fem)
        {
            maleL.setTarget (male); maleR.setTarget (male);
            femL.setTarget (fem);   femR.setTarget (fem);
        }

        void render (float* outL, float* outR, int n, const Settings& s)
        {
            const float invSr = 1.0f / static_cast<float> (sr);
            const bool useMale = s.type != Formants::VoiceType::female;
            const bool useFem  = s.type != Formants::VoiceType::male;
            const float vibDepth = s.vibrato * 0.35f;          // semitones
            const float breath = s.breath;
            const float gain = velGain * 0.16f;

            for (int i = 0; i < n; ++i)
            {
                const float e = env.next();
                if (e <= 0.0f && ! env.isActive()) { note = -1; break; }
                vibOnset = std::min (1.0f, vibOnset + invSr * 1.6f);

                float mL = 0.0f, mR = 0.0f, fL = 0.0f, fR = 0.0f;
                for (auto& sg : singers)
                {
                    sg.driftPhase += sg.driftRate * invSr;   if (sg.driftPhase >= 1.0f) sg.driftPhase -= 1.0f;
                    sg.vibPhase   += sg.vibRate * invSr;     if (sg.vibPhase >= 1.0f)   sg.vibPhase -= 1.0f;
                    const float bend = sg.driftDepth * std::sin (Dsp::twoPi * sg.driftPhase)
                                     + vibDepth * vibOnset * std::sin (Dsp::twoPi * sg.vibPhase);
                    const float f = freq * sg.detune * (1.0f + bend * 0.05776f);   // ~2^(bend/12) for small bends
                    const float dt = std::min (0.45f, f * invSr);
                    sg.phase += dt; if (sg.phase >= 1.0f) sg.phase -= 1.0f;
                    const float x = Dsp::blepSaw (sg.phase, dt);
                    if (sg.male) { mL += x * sg.panL; mR += x * sg.panR; }
                    else         { fL += x * sg.panL; fR += x * sg.panR; }
                }
                const float nz = noise.next() * breath * 2.2f;
                mL += nz; mR += nz; fL += nz; fR += nz;

                float yl = 0.0f, yr = 0.0f;
                if (useMale) { yl += maleL.process (mL); yr += maleR.process (mR); }
                if (useFem)  { yl += femL.process (fL);  yr += femR.process (fR); }

                const float g = e * gain;
                outL[i] += yl * g;
                outR[i] += yr * g;
            }
        }
    };

    void updateFormants (bool force)
    {
        // morph targets glide (one-pole, ~25 ms) so vowel moves sound like singing, not zipper noise
        const auto m = Formants::choirVowel (false, settings.vowel);
        const auto f = Formants::choirVowel (true, settings.vowel);
        const float k = force ? 0.0f : glide;
        for (size_t i = 0; i < Formants::numFormants; ++i)
        {
            curMale.freq[i] = m.freq[i] + (curMale.freq[i] - m.freq[i]) * k;
            curMale.bw[i]   = m.bw[i]   + (curMale.bw[i]   - m.bw[i])   * k;
            curMale.gain[i] = m.gain[i] + (curMale.gain[i] - m.gain[i]) * k;
            curFem.freq[i]  = f.freq[i] + (curFem.freq[i]  - f.freq[i]) * k;
            curFem.bw[i]    = f.bw[i]   + (curFem.bw[i]    - f.bw[i])   * k;
            curFem.gain[i]  = f.gain[i] + (curFem.gain[i]  - f.gain[i]) * k;
        }
        for (auto& v : poly.voices)
            if (v.isActive() || force)
                v.setFormants (curMale, curFem);
    }

    static constexpr int ctrlInterval = 32;
    static constexpr float glide = 0.92f;   // per 32 samples

    double sampleRate = 48000.0;
    Settings settings;
    PolyVoices<Voice, numVoices> poly;
    Formants::Set curMale {}, curFem {};
    int ctrlCountdown = 0;
};
