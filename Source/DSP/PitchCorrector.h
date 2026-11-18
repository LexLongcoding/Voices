// Voices - Copyright (C) 2026 LexLongcoding
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
#include "DspUtils.h"
#include <array>
#include <cstdint>

// Scales for the auto-tune (12-bit masks, bit 0 = root).
namespace Scales
{
    struct Scale { const char* name; uint16_t mask; };

    inline constexpr std::array<Scale, 10> all {{
        { "CHROMATIC",  0b111111111111 },
        { "MAJOR",      0b101010110101 },
        { "MINOR",      0b010110101101 },
        { "HARM MINOR", 0b100110101101 },
        { "MAJ PENTA",  0b001010010101 },
        { "MIN PENTA",  0b010010101001 },
        { "BLUES",      0b010011101001 },
        { "DORIAN",     0b011010101101 },
        { "MIXOLYDIAN", 0b011010110101 },
        { "PHRYGIAN",   0b010110101011 },
    }};

    // Rotate a root-relative mask to absolute pitch classes for the given key (0 = C).
    inline uint16_t inKey (uint16_t mask, int key)
    {
        key = ((key % 12) + 12) % 12;
        return static_cast<uint16_t> (((mask << key) | (mask >> (12 - key))) & 0xFFF);
    }

    // Nearest MIDI note whose pitch class is in `pcMask` (absolute pitch classes).
    inline int nearest (float midi, uint16_t pcMask)
    {
        if (pcMask == 0) pcMask = 0xFFF;
        const int base = static_cast<int> (std::lround (midi));
        for (int d = 0; d <= 6; ++d)
        {
            const int cands[2] = { base + d, base - d };
            int best = -1000;
            float bestDist = 1.0e9f;
            for (int c : cands)
            {
                const int pc = ((c % 12) + 12) % 12;
                if (pcMask & (1u << pc))
                {
                    const float dist = std::abs (midi - static_cast<float> (c));
                    if (dist < bestDist) { bestDist = dist; best = c; }
                }
            }
            if (best > -1000) return best;
        }
        return base;
    }
}

// Decides how far to bend the sung pitch, auto-tune style.
//   retune   0 s  = instant: the hard, stepped "robot" effect
//            ~50 ms = modern polished pop
//            200+ ms = gentle, transparent correction
//   amount   how much of the distance to the target note is corrected
//   humanize lets long held notes relax (keeps natural vibrato) while note
//            changes still snap
class PitchCorrector
{
public:
    struct Settings { float retuneSec = 0.05f; float amount = 1.0f; float humanize = 0.3f; uint16_t pcMask = 0xFFF; };

    void reset() { correction = 0.0f; target = -1; held = 0.0f; outMidi = inMidi = 0.0f; }

    // Call at a steady rate with the latest detection. Returns the correction in semitones.
    float update (bool voiced, float detectedMidi, const Settings& s, float dt)
    {
        if (! voiced)
        {
            held = 0.0f;
            target = -1;
            // let the correction relax toward 0 during unvoiced gaps
            correction *= std::exp (-dt / 0.08f);
            return correction;
        }

        inMidi = detectedMidi;
        const int candidate = Scales::nearest (detectedMidi, s.pcMask);
        if (target < 0)
            target = candidate;
        else if (candidate != target && std::abs (detectedMidi - static_cast<float> (target)) > 0.62f)
        {
            target = candidate;   // hysteresis: only switch when clearly closer to another note
            held = 0.0f;
        }
        else if (candidate != target && (s.pcMask & (1u << (((target % 12) + 12) % 12))) == 0)
            target = candidate;   // scale / key changed under a held note

        held += dt;
        const float desired = (static_cast<float> (target) - detectedMidi) * s.amount;
        float tau = s.retuneSec + s.humanize * 0.3f * std::min (1.0f, std::max (0.0f, held - 0.12f) / 0.4f);
        if (tau <= 0.0005f)
            correction = desired;
        else
            correction += (desired - correction) * (1.0f - std::exp (-dt / tau));

        outMidi = detectedMidi + correction;
        return correction;
    }

    int   getTarget() const { return target; }
    float getInputMidi() const { return inMidi; }
    float getOutputMidi() const { return outMidi; }

private:
    float correction = 0.0f, held = 0.0f, inMidi = 0.0f, outMidi = 0.0f;
    int target = -1;
};
