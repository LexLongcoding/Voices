// Voices - Copyright (C) 2026 LexLongcoding
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
#include "DspUtils.h"
#include <array>

// Vowel formant data and a 5-band parallel formant filter.
// Tables: classic singing-voice formants (tenor for the male voice, alto for the
// female voice) as used in formant synthesis since the 1980s.
namespace Formants
{
    constexpr int numFormants = 5;
    constexpr int numVowels = 5;          // A E I O U
    enum Vowel { A = 0, E, I, O, U };
    enum class VoiceType { male = 0, female = 1, mixed = 2 };

    struct Set
    {
        std::array<float, numFormants> freq, bw, gain;   // Hz, Hz, linear
    };

    struct Row { float f[5]; float db[5]; float bw[5]; };

    inline const Row& row (bool female, int vowel)
    {
        static const Row male[numVowels] = {
            { { 650, 1080, 2650, 2900, 3250 }, { 0,  -6,  -7,  -8, -22 }, { 80, 90, 120, 130, 140 } },   // A
            { { 400, 1700, 2600, 3200, 3580 }, { 0, -14, -12, -14, -20 }, { 70, 80, 100, 120, 120 } },   // E
            { { 290, 1870, 2800, 3250, 3540 }, { 0, -15, -18, -20, -30 }, { 40, 90, 100, 120, 120 } },   // I
            { { 400,  800, 2600, 2800, 3000 }, { 0, -10, -12, -12, -26 }, { 40, 80, 100, 120, 120 } },   // O
            { { 350,  600, 2700, 2900, 3300 }, { 0, -20, -17, -14, -26 }, { 40, 60, 100, 120, 120 } },   // U
        };
        static const Row fem[numVowels] = {
            { { 800, 1150, 2800, 3500, 4950 }, { 0,  -4, -20, -36, -60 }, { 80, 90, 120, 130, 140 } },   // A
            { { 400, 1600, 2700, 3300, 4950 }, { 0, -24, -30, -35, -60 }, { 60, 80, 120, 150, 200 } },   // E
            { { 350, 1700, 2700, 3700, 4950 }, { 0, -20, -30, -36, -60 }, { 50, 100, 120, 150, 200 } },  // I
            { { 450,  800, 2830, 3500, 4950 }, { 0,  -9, -16, -28, -55 }, { 70, 80, 100, 130, 135 } },   // O
            { { 325,  700, 2530, 3500, 4950 }, { 0, -12, -30, -40, -64 }, { 50, 60, 170, 180, 200 } },   // U
        };
        return female ? fem[vowel] : male[vowel];
    }

    inline Set fromRow (const Row& r)
    {
        Set s {};
        for (int i = 0; i < numFormants; ++i)
        {
            s.freq[static_cast<size_t> (i)] = r.f[i];
            s.bw[static_cast<size_t> (i)]   = r.bw[i];
            // lift the upper formants a little: brighter, more "present" vowels
            s.gain[static_cast<size_t> (i)] = Dsp::dbToGain (r.db[i] * 0.75f);
        }
        return s;
    }

    inline Set lerp (const Set& a, const Set& b, float t)
    {
        Set s {};
        for (size_t i = 0; i < numFormants; ++i)
        {
            s.freq[i] = a.freq[i] * std::pow (b.freq[i] / a.freq[i], t);   // log-frequency glide
            s.bw[i]   = a.bw[i] + (b.bw[i] - a.bw[i]) * t;
            s.gain[i] = a.gain[i] + (b.gain[i] - a.gain[i]) * t;
        }
        return s;
    }

    // Continuous vowel position 0..4 (A, E, I, O, U).
    inline Set vowel (bool female, float pos)
    {
        pos = std::clamp (pos, 0.0f, 4.0f);
        const int i0 = std::min (3, static_cast<int> (pos));
        return lerp (fromRow (row (female, i0)), fromRow (row (female, i0 + 1)), pos - static_cast<float> (i0));
    }

    // Choir "OOH .. AAH" morph: 0 = OOH (U), 0.5 = OH (O), 1 = AAH (A).
    inline Set choirVowel (bool female, float t)
    {
        t = std::clamp (t, 0.0f, 1.0f);
        if (t < 0.5f)
            return lerp (fromRow (row (female, U)), fromRow (row (female, O)), t * 2.0f);
        return lerp (fromRow (row (female, O)), fromRow (row (female, A)), (t - 0.5f) * 2.0f);
    }

    // Parallel resonator bank. Coefficients are set from a Set (call every ~32
    // samples while morphing); `shift` multiplies every formant frequency.
    class Bank
    {
    public:
        void prepare (double sr) noexcept { sampleRate = sr; reset(); }
        void reset() noexcept { for (auto& f : filters) f.reset(); }

        void setTarget (const Set& s, float shift = 1.0f, float qScale = 1.0f) noexcept
        {
            for (size_t i = 0; i < numFormants; ++i)
            {
                const float f = s.freq[i] * shift;
                const float q = std::clamp (f / std::max (20.0f, s.bw[i]) * qScale, 0.7f, 40.0f);
                filters[i].setCoefficients (f, q, sampleRate);
                // fade formants that would sit above ~0.42 * fs instead of aliasing
                const float nyqFade = std::clamp ((0.42f * static_cast<float> (sampleRate) - f) / 1500.0f, 0.0f, 1.0f);
                gains[i] = s.gain[i] * nyqFade;
            }
        }

        float process (float x) noexcept
        {
            float y = 0.0f;
            for (size_t i = 0; i < numFormants; ++i)
                y += filters[i].bandpass (x) * gains[i];
            return y;
        }

    private:
        double sampleRate = 48000.0;
        std::array<Dsp::Svf, numFormants> filters {};
        std::array<float, numFormants> gains {};
    };
}
