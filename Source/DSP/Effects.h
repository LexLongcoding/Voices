// Voices - Copyright (C) 2026 LexLongcoding
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
#include "DspUtils.h"
#include <array>
#include <vector>

// The FX section: chorus -> ping-pong delay -> reverb, each with its own mix.
namespace Fx
{
    // Circular delay line with fractional (linear) reads.
    class DelayLine
    {
    public:
        void prepare (int maxSamples)
        {
            size = 1;
            while (size < maxSamples + 4) size <<= 1;
            buf.assign (static_cast<size_t> (size), 0.0f);
            mask = size - 1;
            w = 0;
        }
        void reset() { std::fill (buf.begin(), buf.end(), 0.0f); }
        void push (float x) { buf[static_cast<size_t> (w)] = x; w = (w + 1) & mask; }
        float read (float delay) const   // delay in samples, >= 1
        {
            const float pos = static_cast<float> (w) - std::max (1.0f, delay);
            const float fl = std::floor (pos);
            const float f = pos - fl;
            const int i0 = static_cast<int> (fl) & mask;
            const int i1 = (i0 + 1) & mask;
            return buf[static_cast<size_t> (i0)] + (buf[static_cast<size_t> (i1)] - buf[static_cast<size_t> (i0)]) * f;
        }
        float readInt (int delay) const { return buf[static_cast<size_t> ((w - delay) & mask)]; }

    private:
        std::vector<float> buf;
        int size = 1, mask = 0, w = 0;
    };

    // Lush stereo chorus: three modulated taps per side, phases spread around the circle.
    class Chorus
    {
    public:
        void prepare (double sr)
        {
            sampleRate = sr;
            dl.prepare (static_cast<int> (sr * 0.05));
            dr.prepare (static_cast<int> (sr * 0.05));
            phase = 0.0f;
        }
        void reset() { dl.reset(); dr.reset(); }

        void process (float* l, float* r, int n, float rateHz, float depth, float mix)
        {
            if (mix <= 0.0001f) { dl.reset(); dr.reset(); return; }   // keep it clean when re-enabled
            const float sr = static_cast<float> (sampleRate);
            const float base = 0.011f * sr, dep = (0.0015f + 0.0055f * depth) * sr;
            const float inc = rateHz / sr;
            const float wet = mix, dry = 1.0f - 0.5f * mix;
            for (int i = 0; i < n; ++i)
            {
                dl.push (l[i]);
                dr.push (r[i]);
                phase += inc; if (phase >= 1.0f) phase -= 1.0f;
                float wl = 0.0f, wr = 0.0f;
                for (int k = 0; k < 3; ++k)
                {
                    const float p = phase + static_cast<float> (k) / 3.0f;
                    const float ml = std::sin (Dsp::twoPi * p);
                    const float mr = std::sin (Dsp::twoPi * (p + 0.25f));
                    wl += dl.read (base + dep * (0.5f + 0.5f * ml) + static_cast<float> (k) * 0.0021f * sr);
                    wr += dr.read (base + dep * (0.5f + 0.5f * mr) + static_cast<float> (k) * 0.0017f * sr);
                }
                l[i] = l[i] * dry + wl * 0.36f * wet;
                r[i] = r[i] * dry + wr * 0.36f * wet;
            }
        }

    private:
        double sampleRate = 48000.0;
        DelayLine dl, dr;
        float phase = 0.0f;
    };

    // Tempo-synced ping-pong delay with darkening repeats.
    class PingPongDelay
    {
    public:
        void prepare (double sr)
        {
            sampleRate = sr;
            dl.prepare (static_cast<int> (sr * 2.6));
            dr.prepare (static_cast<int> (sr * 2.6));
            lpL.setCutoff (5200.0f, sr); lpR.setCutoff (5200.0f, sr);
            hpL.setCoefficients (140.0f, 0.7f, sr); hpR.setCoefficients (140.0f, 0.7f, sr);
            reset();
        }
        void reset() { dl.reset(); dr.reset(); lpL.reset(); lpR.reset(); hpL.reset(); hpR.reset(); smoothedTime = -1.0f; }

        void process (float* l, float* r, int n, float timeSec, float feedback, float mix)
        {
            if (mix <= 0.0001f) { if (smoothedTime >= 0.0f) reset(); return; }
            const float target = std::clamp (timeSec, 0.01f, 2.5f) * static_cast<float> (sampleRate);
            if (smoothedTime < 0.0f) smoothedTime = target;
            const float fb = std::clamp (feedback, 0.0f, 0.95f);
            for (int i = 0; i < n; ++i)
            {
                smoothedTime += (target - smoothedTime) * 0.0005f;   // tempo changes glide, no clicks
                const float yl = dl.read (smoothedTime);
                const float yr = dr.read (smoothedTime);
                const float in = 0.5f * (l[i] + r[i]);
                // ping-pong: input enters left, each repeat crosses to the other side
                dl.push (in + hpL.highpass (lpL.process (yr)) * fb);
                dr.push (hpR.highpass (lpR.process (yl)) * fb);
                l[i] += yl * mix;
                r[i] += yr * mix;
            }
        }

    private:
        double sampleRate = 48000.0;
        DelayLine dl, dr;
        Dsp::OnePole lpL, lpR;
        Dsp::Svf hpL, hpR;
        float smoothedTime = -1.0f;
    };

    // 8-line feedback delay network reverb: input diffusers, Hadamard mixing,
    // per-line damping and slow modulation for a smooth, wide, lush tail.
    class Reverb
    {
    public:
        void prepare (double sr)
        {
            sampleRate = sr;
            const float scale = static_cast<float> (sr / 48000.0);
            for (size_t i = 0; i < numLines; ++i)
                lines[i].prepare (static_cast<int> (baseLengths[i] * 1.6f * scale) + 64);
            for (size_t i = 0; i < numDiff; ++i)
                diff[i].prepare (static_cast<int> (diffLengths[i] * scale) + 4);
            pre.prepare (static_cast<int> (sr * 0.05));
            inHp.setCoefficients (120.0f, 0.7f, sr);
            reset();
        }

        void reset()
        {
            for (auto& d : lines) d.reset();
            for (auto& d : diff) d.reset();
            pre.reset();
            for (auto& f : damp) f.reset();
            inHp.reset();
            lfo = 0.0f;
            active = false;
        }

        void process (float* l, float* r, int n, float size, float damping, float mix)
        {
            if (mix <= 0.0001f) { if (active) reset(); return; }
            active = true;
            const float sc = static_cast<float> (sampleRate / 48000.0);
            const float lenScale = 0.55f + 0.95f * size;
            const float rt60 = 0.9f + size * size * 8.0f;                  // seconds
            const float sr = static_cast<float> (sampleRate);
            std::array<float, numLines> len {}, g {};
            for (size_t i = 0; i < numLines; ++i)
            {
                len[i] = baseLengths[i] * lenScale * sc;
                g[i] = std::pow (0.001f, len[i] / (rt60 * sr));
            }
            const float dampHz = 16000.0f - damping * 13500.0f;
            for (auto& f : damp) f.setCutoff (dampHz, sampleRate);
            const float preDelay = 0.018f * sr;
            const float lfoInc = 0.31f / sr;
            const float modDepth = 6.0f * sc;

            for (int i = 0; i < n; ++i)
            {
                pre.push (inHp.highpass (0.5f * (l[i] + r[i])));
                float x = pre.read (preDelay);

                // series all-pass diffusers smear the attack into a dense cloud
                for (size_t d = 0; d < numDiff; ++d)
                {
                    const float delayed = diff[d].readInt (static_cast<int> (diffLengths[d] * sc));
                    const float v = x + delayed * 0.62f;
                    diff[d].push (v);
                    x = delayed - v * 0.62f;
                }

                lfo += lfoInc; if (lfo >= 1.0f) lfo -= 1.0f;
                const float m1 = std::sin (Dsp::twoPi * lfo) * modDepth;
                const float m2 = std::sin (Dsp::twoPi * (lfo + 0.37f)) * modDepth;

                std::array<float, numLines> y {};
                for (size_t k = 0; k < numLines; ++k)
                {
                    const float mod = k == 1 ? m1 : (k == 6 ? m2 : 0.0f);
                    y[k] = damp[k].process (lines[k].read (len[k] + mod));
                }

                // fast Walsh-Hadamard (orthogonal, energy preserving), scaled by 1/sqrt(8)
                hadamard (y);
                for (size_t k = 0; k < numLines; ++k)
                    lines[k].push (y[k] * g[k] + x * inGain[k]);

                const float outL = (y[0] - y[2] + y[4] - y[6]) * 0.5f;
                const float outR = (y[1] - y[3] + y[5] - y[7]) * 0.5f;
                l[i] = l[i] * (1.0f - 0.45f * mix) + outL * mix * 0.9f;
                r[i] = r[i] * (1.0f - 0.45f * mix) + outR * mix * 0.9f;
            }
        }

    private:
        static constexpr size_t numLines = 8, numDiff = 4;

        static void hadamard (std::array<float, numLines>& a)
        {
            for (size_t h = 1; h < numLines; h <<= 1)
                for (size_t i = 0; i < numLines; i += h << 1)
                    for (size_t j = i; j < i + h; ++j)
                    {
                        const float x = a[j], y = a[j + h];
                        a[j] = x + y;
                        a[j + h] = x - y;
                    }
            for (auto& v : a) v *= 0.35355339f;
        }

        static constexpr float baseLengths[numLines] = { 1153, 1327, 1559, 1733, 1951, 2111, 2357, 2539 };
        static constexpr float diffLengths[numDiff]  = { 142, 107, 379, 277 };
        static constexpr float inGain[numLines] = { 1, -1, 1, -1, -1, 1, -1, 1 };

        double sampleRate = 48000.0;
        std::array<DelayLine, numLines> lines;
        std::array<DelayLine, numDiff> diff;
        std::array<Dsp::OnePole, numLines> damp;
        DelayLine pre;
        Dsp::Svf inHp;
        float lfo = 0.0f;
        bool active = false;
    };
}
