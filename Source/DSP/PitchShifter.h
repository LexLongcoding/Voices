// Voices - Copyright (C) 2026 LexLongcoding
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
#include "DspUtils.h"
#include <vector>

// Real-time TD-PSOLA pitch shifter with independent formant control.
//
// Analysis marks are placed one pitch period apart on the waveform peaks of the
// input. Output grains (Hann-windowed, two periods long) are laid down at the
// *new* period, so the pitch changes while the vocal-tract formants stay put -
// the basis for natural pitch correction. Reading each grain faster/slower
// (resampling it) moves the formants: that's the voice changer's "gender" control.
//
// Fixed latency: getLatency() samples (about 12 ms).
class PitchShifter
{
public:
    void prepare (double sr)
    {
        sampleRate = sr;
        latency = static_cast<int> (std::lround (sr * 0.012));
        size = 1;
        while (size < static_cast<int> (sr * 0.25)) size <<= 1;   // 250 ms of history
        mask = size - 1;
        in.assign (static_cast<size_t> (size), 0.0f);
        lp.assign (static_cast<size_t> (size), 0.0f);
        acc.assign (static_cast<size_t> (size), 0.0f);
        wsum.assign (static_cast<size_t> (size), 0.0f);
        lpf.setCoefficients (900.0f, 0.6f, sr);
        unvoicedPeriod = static_cast<float> (sr * 0.006);
        reset();
    }

    void reset()
    {
        std::fill (in.begin(), in.end(), 0.0f);
        std::fill (lp.begin(), lp.end(), 0.0f);
        std::fill (acc.begin(), acc.end(), 0.0f);
        std::fill (wsum.begin(), wsum.end(), 0.0f);
        lpf.reset();
        now = 0;
        numMarks = 0;
        markHead = 0;
        nextMarkGuess = static_cast<double> (unvoicedPeriod);
        nextSynth = 0.0;
        period = unvoicedPeriod;
        voiced = false;
    }

    int getLatency() const { return latency; }

    // Per block (or sub-block): the detected input period (samples, 0 = unvoiced),
    // the pitch ratio to apply and the formant ratio (>1 = formants up).
    void setControl (float inputPeriod, float pitchRatio, float formantRatio)
    {
        voiced = inputPeriod > 0.0f;
        period = voiced ? std::clamp (inputPeriod, 30.0f, static_cast<float> (sampleRate / 60.0)) : unvoicedPeriod;
        ratio = voiced ? std::clamp (pitchRatio, 0.25f, 4.0f) : 1.0f;
        formant = std::clamp (formantRatio, 0.5f, 2.0f);
    }

    // Process one sample: push input, return the shifted output (delayed by getLatency()).
    float process (float x)
    {
        const int w = static_cast<int> (now & mask);
        in[static_cast<size_t> (w)] = x;
        lp[static_cast<size_t> (w)] = lpf.lowpass (x);
        ++now;

        placeAnalysisMarks();

        // output time = input time - latency
        const double outTime = static_cast<double> (now - 1 - latency);
        while (nextSynth - static_cast<double> (period) / formant <= outTime + 1.0)
            placeGrain();

        const int o = static_cast<int> (static_cast<int64_t> (outTime) & mask);
        const float a = acc[static_cast<size_t> (o)], ws = wsum[static_cast<size_t> (o)];
        acc[static_cast<size_t> (o)] = 0.0f;
        wsum[static_cast<size_t> (o)] = 0.0f;
        // sqrt normalisation: roughly loudness-neutral across pitch ratios (grains add partly incoherently)
        return a * gainComp / std::clamp (std::sqrt (ws), 0.75f, 2.0f);
    }

private:
    void placeAnalysisMarks()
    {
        // a mark can be finalised once we have seen half a period past its guess
        const double half = 0.5 * period;
        while (nextMarkGuess + half < static_cast<double> (now))
        {
            double mark = nextMarkGuess;
            if (voiced)
            {
                // snap to the strongest peak of the low-passed signal around the guess
                const int64_t lo = static_cast<int64_t> (nextMarkGuess - 0.3 * period);
                const int64_t hi = static_cast<int64_t> (nextMarkGuess + 0.3 * period);
                float best = -1.0e9f;
                int64_t bestPos = static_cast<int64_t> (nextMarkGuess);
                for (int64_t t = std::max<int64_t> (lo, now - size + 1); t <= hi && t < now; ++t)
                {
                    const float v = lp[static_cast<size_t> (t & mask)];
                    if (v > best) { best = v; bestPos = t; }
                }
                mark = static_cast<double> (bestPos);
                if (numMarks > 0 && mark < lastMark() + 0.5 * period)
                    mark = lastMark() + period;   // never bunch marks together
            }
            pushMark (mark);
            nextMarkGuess = mark + period;
        }
    }

    void placeGrain()
    {
        const double ts = nextSynth;
        const float ls = period / formant;               // output half-length
        const double reach = static_cast<double> (period);  // input half-length read
        const double newest = static_cast<double> (now - 1) - reach - 2.0;

        // choose the analysis mark nearest to ts that is fully available
        double m = -1.0;
        double bestDist = 1.0e18;
        for (int i = 0; i < numMarks; ++i)
        {
            const double mk = marks[static_cast<size_t> ((markHead - 1 - i + maxMarks) % maxMarks)];
            if (mk > newest) continue;
            const double d = std::abs (mk - ts);
            if (d < bestDist) { bestDist = d; m = mk; }
            else if (mk < ts) break;   // marks are ordered newest -> oldest
        }
        if (m < 0.0) m = std::min (ts, newest);

        const int n = static_cast<int> (ls);
        const float invLs = 1.0f / ls;
        for (int k = -n; k <= n; ++k)
        {
            const float wv = 0.5f + 0.5f * std::cos (Dsp::pi * static_cast<float> (k) * invLs);
            const double src = m + static_cast<double> (k) * formant;
            const float v = readInterp (src);
            const int64_t t = static_cast<int64_t> (std::floor (ts)) + k;
            const int idx = static_cast<int> (t & mask);
            acc[static_cast<size_t> (idx)] += v * wv;
            wsum[static_cast<size_t> (idx)] += wv;
        }
        nextSynth += static_cast<double> (period) / ratio;

        // if we fell far behind (e.g. after a long unvoiced pause) catch up
        const double outTime = static_cast<double> (now - 1 - latency);
        if (nextSynth < outTime - period) nextSynth = outTime;
    }

    float readInterp (double pos) const
    {
        const double fl = std::floor (pos);
        const float f = static_cast<float> (pos - fl);
        const int64_t i = static_cast<int64_t> (fl);
        if (i < now - size + 2 || i + 2 >= now) return 0.0f;
        const float xm1 = in[static_cast<size_t> ((i - 1) & mask)], x0 = in[static_cast<size_t> (i & mask)],
                    x1 = in[static_cast<size_t> ((i + 1) & mask)], x2 = in[static_cast<size_t> ((i + 2) & mask)];
        // 4-point Hermite
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }

    void pushMark (double m)
    {
        marks[static_cast<size_t> (markHead)] = m;
        markHead = (markHead + 1) % maxMarks;
        numMarks = std::min (numMarks + 1, maxMarks);
    }
    double lastMark() const { return marks[static_cast<size_t> ((markHead - 1 + maxMarks) % maxMarks)]; }

    static constexpr int maxMarks = 64;

    double sampleRate = 48000.0;
    int latency = 576, size = 16384, mask = 16383;
    std::vector<float> in, lp, acc, wsum;
    Dsp::Svf lpf;
    int64_t now = 0;
    double marks[maxMarks] {};
    int numMarks = 0, markHead = 0;
    double nextMarkGuess = 0.0, nextSynth = 0.0;
    float period = 288.0f, unvoicedPeriod = 288.0f, ratio = 1.0f, formant = 1.0f;
    static constexpr float gainComp = 1.08f;
    bool voiced = false;
};
