// Voices - Copyright (C) 2026 LexLongcoding
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
#include "DspUtils.h"
#include <vector>

// Monophonic pitch tracker for the singing voice (YIN, de Cheveigne & Kawahara 2002).
// The coarse search runs on a ~12 kHz decimated copy (cheap), then the period is
// refined at the full sample rate so high notes stay in tune.
// Range 65 Hz .. 1100 Hz. A new estimate is produced every ~4 ms.
class PitchDetector
{
public:
    static constexpr float minHz = 65.0f, maxHz = 1100.0f;

    void prepare (double sr)
    {
        sampleRate = sr;
        decim = std::max (1, static_cast<int> (std::lround (sr / 12000.0)));
        rate = sr / decim;
        tauMax = static_cast<int> (rate / minHz) + 2;
        tauMin = std::max (2, static_cast<int> (rate / maxHz) - 1);
        window = std::max (tauMax + 8, static_cast<int> (rate * 0.024));
        hop = std::max (16, static_cast<int> (rate * 0.004));

        dec.assign (static_cast<size_t> (window + tauMax + 4), 0.0f);
        diff.assign (static_cast<size_t> (tauMax + 2), 0.0f);
        fullSize = 1;
        while (fullSize < static_cast<int> (sr / minHz) * 4 + 64) fullSize <<= 1;
        full.assign (static_cast<size_t> (fullSize), 0.0f);

        aa1.setCoefficients (static_cast<float> (rate) * 0.42f, 0.54f, sr);
        aa2.setCoefficients (static_cast<float> (rate) * 0.42f, 1.31f, sr);
        hp.setCoefficients (50.0f, 0.7f, sr);
        reset();
    }

    void reset()
    {
        std::fill (dec.begin(), dec.end(), 0.0f);
        std::fill (full.begin(), full.end(), 0.0f);
        aa1.reset(); aa2.reset(); hp.reset();
        decCount = hopCount = 0; fullPos = 0;
        voiced = false; freq = 0.0f; confidence = 0.0f;
        history[0] = history[1] = history[2] = 0.0f;
        level = 0.0f;
    }

    // Feed one sample; returns true when a new estimate is ready.
    bool push (float x)
    {
        x = hp.highpass (x);
        full[static_cast<size_t> (fullPos)] = x;
        fullPos = (fullPos + 1) & (fullSize - 1);
        level = std::max (std::abs (x), level * levelDecay);

        const float y = aa2.lowpass (aa1.lowpass (x));
        if (++decCount < decim)
            return false;
        decCount = 0;

        // shift-free circular use would complicate YIN; the buffer is small, so slide it
        std::move (dec.begin() + 1, dec.end(), dec.begin());
        dec.back() = y;

        if (++hopCount < hop)
            return false;
        hopCount = 0;
        analyse();
        return true;
    }

    bool  isVoiced() const     { return voiced; }
    float getFrequency() const { return freq; }        // Hz (valid while voiced)
    float getPeriod() const    { return voiced ? static_cast<float> (sampleRate) / freq : 0.0f; }   // samples
    float getConfidence() const{ return confidence; }  // 0..1

private:
    void analyse()
    {
        const int n = static_cast<int> (dec.size());
        const float* x = dec.data() + (n - window - tauMax);   // oldest sample of the analysed span

        // gate: ignore near-silence (-50 dBFS) so breaths and room noise aren't "tuned"
        if (level < 0.0032f)
        {
            setUnvoiced();
            return;
        }

        // difference function + cumulative mean normalisation
        diff[0] = 1.0f;
        float running = 0.0f;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            float d = 0.0f;
            for (int j = 0; j < window; ++j)
            {
                const float delta = x[j] - x[j + tau];
                d += delta * delta;
            }
            running += d;
            diff[static_cast<size_t> (tau)] = running > 0.0f ? d * static_cast<float> (tau) / running : 1.0f;
        }

        // first dip under the threshold, walked down to its local minimum
        int best = -1;
        for (int tau = tauMin; tau < tauMax; ++tau)
            if (diff[static_cast<size_t> (tau)] < threshold)
            {
                while (tau + 1 < tauMax && diff[static_cast<size_t> (tau + 1)] < diff[static_cast<size_t> (tau)]) ++tau;
                best = tau;
                break;
            }
        if (best < 0)
        {
            setUnvoiced();
            return;
        }

        // parabolic interpolation on the decimated curve
        const float a = diff[static_cast<size_t> (best - 1)], b = diff[static_cast<size_t> (best)],
                    c = diff[static_cast<size_t> (best + 1)];
        const float den = a - 2.0f * b + c;
        const float frac = std::abs (den) > 1.0e-9f ? std::clamp (0.5f * (a - c) / den, -0.5f, 0.5f) : 0.0f;
        float period = (static_cast<float> (best) + frac) * static_cast<float> (decim);

        if (decim > 1)
            period = refine (period);

        float f = static_cast<float> (sampleRate) / period;
        if (f < minHz * 0.95f || f > maxHz * 1.05f)
        {
            setUnvoiced();
            return;
        }

        // 3-point median kills single-frame octave errors
        history[0] = history[1]; history[1] = history[2]; history[2] = f;
        if (history[0] > 0.0f && history[1] > 0.0f)
        {
            const float lo = std::min ({ history[0], history[1], history[2] });
            const float hi = std::max ({ history[0], history[1], history[2] });
            f = history[0] + history[1] + history[2] - lo - hi;
        }
        freq = f;
        voiced = true;
        confidence = std::clamp (1.0f - b * 4.0f, 0.0f, 1.0f);
    }

    // Search +-decim samples around the coarse period at the full rate.
    float refine (float coarse)
    {
        const int centre = static_cast<int> (std::lround (coarse));
        const int span = decim + 1;
        const int lo = std::max (2, centre - span), hi = centre + span;
        const int w = std::min (std::max (centre * 2, 256), fullSize / 2 - hi - 2);
        if (w < 32) return coarse;

        float d[64];
        const int count = std::min (64, hi - lo + 1);
        for (int k = 0; k < count; ++k)
        {
            const int tau = lo + k;
            float s = 0.0f;
            for (int j = 0; j < w; ++j)
            {
                const float a = full[static_cast<size_t> ((fullPos - 1 - j + fullSize) & (fullSize - 1))];
                const float b = full[static_cast<size_t> ((fullPos - 1 - j - tau + 2 * fullSize) & (fullSize - 1))];
                s += (a - b) * (a - b);
            }
            d[k] = s;
        }
        int bi = 0;
        for (int k = 1; k < count; ++k) if (d[k] < d[bi]) bi = k;
        if (bi == 0 || bi == count - 1) return static_cast<float> (lo + bi);
        const float den = d[bi - 1] - 2.0f * d[bi] + d[bi + 1];
        const float frac = std::abs (den) > 1.0e-12f ? std::clamp (0.5f * (d[bi - 1] - d[bi + 1]) / den, -0.5f, 0.5f) : 0.0f;
        return static_cast<float> (lo + bi) + frac;
    }

    void setUnvoiced()
    {
        voiced = false;
        confidence = 0.0f;
        history[0] = history[1] = history[2] = 0.0f;
    }

    static constexpr float threshold = 0.15f;
    static constexpr float levelDecay = 0.9995f;

    double sampleRate = 48000.0, rate = 12000.0;
    int decim = 4, tauMax = 180, tauMin = 10, window = 300, hop = 48;
    std::vector<float> dec, diff, full;
    int fullSize = 4096, fullPos = 0, decCount = 0, hopCount = 0;
    Dsp::Svf aa1, aa2, hp;
    bool voiced = false;
    float freq = 0.0f, confidence = 0.0f, level = 0.0f;
    float history[3] {};
};
