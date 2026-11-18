// Voices - Copyright (C) 2026 LexLongcoding
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
#include "DspUtils.h"
#include <array>
#include <cstdint>

// Arpeggiator for the synth engines (choir, vocal synth, vocoder carrier).
// Modes: TREM (the held chord repeats), UP, DOWN, RANDOM. Range 1-4 octaves.
// Follows the host tempo; synced to the bar grid while the host plays,
// free-running (starting the moment you press a key) when it is stopped.
class Arpeggiator
{
public:
    enum class Mode { trem = 0, up = 1, down = 2, random = 3 };

    struct Settings
    {
        Mode mode = Mode::up;
        int octaves = 1;
        double rateBeats = 0.25;   // step length in quarter notes
        float gate = 0.6f;
        bool latch = false;
    };

    struct Event { int pos; int note; float velocity; bool on; };
    static constexpr int maxEvents = 128;

    void prepare (double sr) { sampleRate = sr; reset(); }

    void reset()
    {
        numHeld = numDown = numSounding = 0;
        running = false;
        stepIndex = 0;
        lastStep = -1;
        offAt = -1;
    }

    void setSettings (const Settings& s) { settings = s; settings.octaves = std::clamp (s.octaves, 1, 4); }

    // Key input (sample position within the current block).
    void keyOn (int note, float velocity, int pos)
    {
        if (settings.latch && numDown == 0)
            numHeld = 0;                      // first key of a new chord replaces the latched one
        addUnique (down, numDown, note);
        addUnique (held, numHeld, note);
        lastVelocity = velocity;
        if (! running) { running = true; startPending = true; startPos = pos; }
    }

    void keyOff (int note)
    {
        removeNote (down, numDown, note);
        if (! settings.latch)
            removeNote (held, numHeld, note);
    }

    void setLatch (bool on)
    {
        if (settings.latch && ! on)   // un-latching drops notes that aren't physically held
        {
            numHeld = 0;
            for (int i = 0; i < numDown; ++i) addUnique (held, numHeld, down[static_cast<size_t> (i)]);
        }
        settings.latch = on;
    }

    // Produce note events for this block. bpm > 0; ppq = host position at block start
    // (use hostPlaying = false when the transport is stopped / unknown).
    int process (int numSamples, double bpm, bool hostPlaying, double ppq, Event* out)
    {
        int count = 0;
        const double beatsPerSample = bpm / 60.0 / sampleRate;
        const double step = settings.rateBeats;
        const auto gateSamples = [&] { return std::max (1.0, settings.gate * step / beatsPerSample); };

        if (numHeld == 0)
        {
            if (numSounding > 0) count = releaseSounding (0, out, count);
            running = false;
            offAt = -1;
            return count;
        }

        if (! hostPlaying)
        {
            if (startPending)
            {
                freeBeat = -static_cast<double> (startPos) * beatsPerSample;   // a step lands exactly on the key press
                lastStep = -1;
                stepIndex = 0;
                startPending = false;
            }
            ppq = freeBeat;
        }
        else
            startPending = false;

        for (int i = 0; i < numSamples; ++i)
        {
            const double beat = ppq + static_cast<double> (i) * beatsPerSample;
            const int64_t s = static_cast<int64_t> (std::floor (beat / step + 1.0e-9));

            if (offAt >= 0 && --offAt == 0 && numSounding > 0)
            {
                count = releaseSounding (i, out, count);
                offAt = -1;
            }

            if (s != lastStep && beat >= 0.0)
            {
                lastStep = s;
                if (numSounding > 0) count = releaseSounding (i, out, count);
                count = playStep (i, out, count);
                offAt = static_cast<int64_t> (gateSamples());
            }
            if (count > maxEvents - 16) break;
        }

        if (! hostPlaying)
            freeBeat += static_cast<double> (numSamples) * beatsPerSample;
        return count;
    }

    int getStepDisplay() const { return displayStep; }   // for the UI step LEDs
    int getNumHeld() const { return numHeld; }

private:
    int playStep (int pos, Event* out, int count)
    {
        sortHeld();
        const int octs = settings.octaves;
        const int total = numHeld * octs;
        displayStep = stepIndex;

        auto noteAt = [&] (int idx) { return sorted[static_cast<size_t> (idx % numHeld)] + 12 * (idx / numHeld); };

        switch (settings.mode)
        {
            case Mode::trem:
            {
                const int oct = stepIndex % octs;
                for (int k = 0; k < numHeld; ++k)
                    count = emitOn (pos, sorted[static_cast<size_t> (k)] + 12 * oct, out, count);
                break;
            }
            case Mode::up:   count = emitOn (pos, noteAt (stepIndex % total), out, count); break;
            case Mode::down: count = emitOn (pos, noteAt (total - 1 - (stepIndex % total)), out, count); break;
            case Mode::random:
            {
                int idx = static_cast<int> (rng.uniform() * static_cast<float> (total)) % total;
                if (total > 1 && idx == lastRandom) idx = (idx + 1 + static_cast<int> (rng.uniform() * static_cast<float> (total - 1))) % total;
                lastRandom = idx;
                count = emitOn (pos, noteAt (idx), out, count);
                break;
            }
        }
        ++stepIndex;
        return count;
    }

    int emitOn (int pos, int note, Event* out, int count)
    {
        if (note > 127 || count >= maxEvents) return count;
        out[count++] = { pos, note, lastVelocity, true };
        if (numSounding < static_cast<int> (sounding.size())) sounding[static_cast<size_t> (numSounding++)] = note;
        return count;
    }

    int releaseSounding (int pos, Event* out, int count)
    {
        for (int k = 0; k < numSounding && count < maxEvents; ++k)
            out[count++] = { pos, sounding[static_cast<size_t> (k)], 0.0f, false };
        numSounding = 0;
        return count;
    }

    static bool addUnique (std::array<int, 32>& a, int& n, int note)
    {
        for (int i = 0; i < n; ++i) if (a[static_cast<size_t> (i)] == note) return false;
        if (n < 32) { a[static_cast<size_t> (n++)] = note; return true; }
        return false;
    }
    static void removeNote (std::array<int, 32>& a, int& n, int note)
    {
        for (int i = 0; i < n; ++i)
            if (a[static_cast<size_t> (i)] == note)
            {
                for (int j = i; j < n - 1; ++j) a[static_cast<size_t> (j)] = a[static_cast<size_t> (j + 1)];
                --n;
                return;
            }
    }
    void sortHeld()
    {
        for (int i = 0; i < numHeld; ++i) sorted[static_cast<size_t> (i)] = held[static_cast<size_t> (i)];
        std::sort (sorted.begin(), sorted.begin() + numHeld);
    }

    double sampleRate = 48000.0;
    Settings settings;
    std::array<int, 32> held {}, down {}, sorted {}, sounding {};
    int numHeld = 0, numDown = 0, numSounding = 0;
    bool running = false, startPending = false;
    int startPos = 0, stepIndex = 0, lastRandom = -1, displayStep = 0;
    int64_t lastStep = -1, offAt = -1;
    double freeBeat = 0.0;
    float lastVelocity = 0.8f;
    Dsp::Noise rng { 0xA2B9u };
};
