// Voices - Copyright (C) 2026 LexLongcoding
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// Small, allocation-free DSP helpers shared by every Voices engine.
// JUCE-free so the engines can be unit-tested on their own (see Tests/).
namespace Dsp
{
    constexpr float pi    = 3.14159265359f;
    constexpr float twoPi = 6.28318530718f;

    inline float midiToHz (float note) noexcept   { return 440.0f * std::exp2 ((note - 69.0f) / 12.0f); }
    inline float hzToMidi (float hz) noexcept     { return 69.0f + 12.0f * std::log2 (std::max (hz, 1.0f) / 440.0f); }
    inline float semisToRatio (float st) noexcept { return std::exp2 (st / 12.0f); }
    inline float dbToGain (float db) noexcept     { return std::pow (10.0f, db / 20.0f); }

    // One-pole smoothing coefficient for a time constant in seconds.
    inline float smoothCoeff (float seconds, double sampleRate) noexcept
    {
        if (seconds <= 0.0f) return 0.0f;
        return std::exp (-1.0f / (seconds * static_cast<float> (sampleRate)));
    }

    // Fast deterministic white noise (xorshift32), output in [-1, 1).
    class Noise
    {
    public:
        Noise() = default;
        explicit Noise (uint32_t seed) : state (seed ? seed : 1u) {}
        void seed (uint32_t s) noexcept { state = s ? s : 1u; }

        float next() noexcept
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return static_cast<float> (static_cast<int32_t> (state)) * (1.0f / 2147483648.0f);
        }
        float uniform() noexcept { return 0.5f * (next() + 1.0f); }   // [0, 1)

    private:
        uint32_t state = 0x9E3779B9u;
    };

    // Topology-preserving-transform state variable filter (Zavalishin / Simper).
    class Svf
    {
    public:
        struct Out { float low, band, high, bandNorm; };   // bandNorm: unity gain at the centre

        void setCoefficients (float cutoffHz, float q, double sampleRate) noexcept
        {
            const float sr = static_cast<float> (sampleRate);
            const float fc = std::clamp (cutoffHz, 10.0f, 0.45f * sr);
            const float g = std::tan (pi * fc / sr);
            k  = 1.0f / std::max (q, 0.1f);
            a1 = 1.0f / (1.0f + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
        }

        void reset() noexcept { ic1 = ic2 = 0.0f; }

        Out process (float x) noexcept
        {
            const float v3 = x - ic2;
            const float v1 = a1 * ic1 + a2 * v3;
            const float v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            return { v2, v1, x - k * v1 - v2, k * v1 };
        }

        float bandpass (float x) noexcept { return process (x).bandNorm; }
        float lowpass (float x) noexcept  { return process (x).low; }
        float highpass (float x) noexcept { return process (x).high; }

    private:
        float k = 1.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
        float ic1 = 0.0f, ic2 = 0.0f;
    };

    // One-pole low-pass (6 dB/oct).
    class OnePole
    {
    public:
        void setCutoff (float hz, double sampleRate) noexcept
        {
            b = std::exp (-twoPi * std::clamp (hz, 1.0f, 0.49f * static_cast<float> (sampleRate))
                          / static_cast<float> (sampleRate));
        }
        void reset (float v = 0.0f) noexcept { z = v; }
        float process (float x) noexcept { z = x + b * (z - x); return z; }
        float state() const noexcept { return z; }

    private:
        float b = 0.0f, z = 0.0f;
    };

    // PolyBLEP correction for band-limited saw / pulse oscillators.
    inline float polyBlep (float t, float dt) noexcept
    {
        if (t < dt)        { t /= dt; return t + t - t * t - 1.0f; }
        if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
        return 0.0f;
    }

    // Band-limited saw: phase in [0,1), dt = freq / sampleRate.
    inline float blepSaw (float phase, float dt) noexcept
    {
        return 2.0f * phase - 1.0f - polyBlep (phase, dt);
    }

    // Band-limited pulse with the given width.
    inline float blepPulse (float phase, float dt, float width = 0.5f) noexcept
    {
        float v = phase < width ? 1.0f : -1.0f;
        v += polyBlep (phase, dt);
        float t2 = phase - width;
        if (t2 < 0.0f) t2 += 1.0f;
        v -= polyBlep (t2, dt);
        return v;
    }

    // Transparent below `knee`, then rolls off smoothly toward 1.0.
    inline float softLimit (float x, float knee = 0.8f) noexcept
    {
        const float a = std::abs (x);
        if (a <= knee)
            return x;
        const float headroom = 1.0f - knee;
        const float y = knee + headroom * std::tanh ((a - knee) / headroom);
        return x < 0.0f ? -y : y;
    }

    // Equal-power pan: pos -1 (left) .. +1 (right).
    inline void panGains (float pos, float& l, float& r) noexcept
    {
        const float a = (std::clamp (pos, -1.0f, 1.0f) + 1.0f) * 0.25f * pi;
        l = std::cos (a);
        r = std::sin (a);
    }

    // Simple attack / release envelope (sustain = 1) with exponential segments.
    class AREnvelope
    {
    public:
        void setTimes (float attackSec, float releaseSec, double sampleRate) noexcept
        {
            const float sr = static_cast<float> (sampleRate);
            attackStep = 1.0f / std::max (1.0f, attackSec * sr);
            releaseCoeff = std::exp (-4.6f / std::max (1.0f, releaseSec * sr));   // -40 dB at releaseSec
        }
        // retriggers continue from the current level (no click)
        void noteOn() noexcept  { lin = 1.0f - std::sqrt (std::max (0.0f, 1.0f - value)); stage = Stage::attack; }
        void noteOff() noexcept { if (stage != Stage::idle) stage = Stage::release; }
        void kill() noexcept    { stage = Stage::idle; value = 0.0f; }
        bool isActive() const noexcept   { return stage != Stage::idle; }
        bool isReleasing() const noexcept { return stage == Stage::release; }
        float getValue() const noexcept  { return value; }

        float next() noexcept
        {
            switch (stage)
            {
                case Stage::attack:
                    // slightly convex attack curve - softer, more "breathed in" than linear
                    lin = std::min (1.0f, lin + attackStep);
                    value = std::max (value, lin * (2.0f - lin));
                    if (lin >= 1.0f) { stage = Stage::sustain; value = 1.0f; }
                    break;
                case Stage::sustain: value = 1.0f; break;
                case Stage::release:
                    value *= releaseCoeff;
                    lin = value;
                    if (value < 1.0e-4f) { stage = Stage::idle; value = 0.0f; lin = 0.0f; }
                    break;
                case Stage::idle: break;
            }
            return value;
        }


    private:
        enum class Stage { idle, attack, sustain, release };
        Stage stage = Stage::idle;
        float value = 0.0f, lin = 0.0f, attackStep = 0.001f, releaseCoeff = 0.999f;
    };
}
