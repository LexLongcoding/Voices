// Voices - Copyright (C) 2026 LexLongcoding
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
#include <utility>
#include <vector>

// Built-in patches. Each lists only what differs from the defaults (real units:
// seconds, 0..1 amounts, semitones, choice indices); everything else loads at its default.
// Voice types: 0 male, 1 female, 2 mixed. Scales: 0 chromatic, 1 major, 2 minor...
namespace FactoryPatches
{
    struct Patch
    {
        const char* name;
        std::vector<std::pair<const char*, float>> values;
    };

    inline const std::vector<Patch>& all()
    {
        static const std::vector<Patch> patches {
            { "INIT", {} },

            { "CATHEDRAL AAHS", {
                { "choir_vowel", 1.0f }, { "choir_size", 0.75f }, { "choir_attack", 0.45f }, { "choir_release", 1.6f },
                { "choir_breath", 0.2f }, { "chorus_mix", 0.4f }, { "reverb_size", 0.9f }, { "reverb_mix", 0.45f },
                { "delay_mix", 0.08f } } },

            { "VOICE OOHS", {
                { "master_voicetype", 1.0f }, { "choir_vowel", 0.0f }, { "choir_size", 0.5f }, { "choir_vibrato", 0.4f },
                { "choir_attack", 0.3f }, { "reverb_mix", 0.35f } } },

            { "SUBTLE POLISH", {
                { "tune_scale", 1.0f }, { "tune_retune", 0.12f }, { "tune_amount", 0.8f }, { "tune_humanize", 0.7f },
                { "choir_on", 0.0f }, { "chorus_mix", 0.0f }, { "delay_mix", 0.0f }, { "reverb_mix", 0.15f }, { "reverb_size", 0.4f } } },

            { "HARD TUNE // TRAP", {
                { "tune_scale", 2.0f }, { "tune_key", 9.0f }, { "tune_retune", 0.0f }, { "tune_amount", 1.0f },
                { "tune_humanize", 0.0f }, { "choir_on", 0.0f }, { "chorus_mix", 0.15f }, { "delay_time", 3.0f },
                { "delay_mix", 0.18f }, { "delay_feedback", 0.3f }, { "reverb_mix", 0.2f } } },

            { "ROBOT CHOIR", {
                { "voc_on", 1.0f }, { "voc_carrier", 2.0f }, { "voc_bands", 24.0f }, { "voice_level", 0.0f },
                { "choir_level", 0.35f }, { "choir_vowel", 0.5f }, { "tune_scale", 1.0f }, { "tune_retune", 0.0f },
                { "chorus_mix", 0.45f }, { "reverb_mix", 0.35f } } },

            { "DAFT VOCODER", {
                { "voc_on", 1.0f }, { "voc_carrier", 0.0f }, { "voc_bands", 16.0f }, { "voc_release", 0.04f },
                { "voc_sibilance", 0.6f }, { "voice_level", 0.0f }, { "choir_on", 0.0f }, { "tune_on", 0.0f },
                { "master_voicetype", 0.0f }, { "delay_time", 2.0f }, { "delay_mix", 0.2f }, { "reverb_mix", 0.2f } } },

            { "DIVA LEAD", {
                { "vsyn_on", 1.0f }, { "vsyn_vowel", 0.0f }, { "vsyn_vibrato", 0.55f }, { "vsyn_bright", 0.6f },
                { "vsyn_glide", 0.06f }, { "choir_on", 0.0f }, { "master_voicetype", 1.0f },
                { "delay_mix", 0.22f }, { "delay_time", 3.0f }, { "reverb_mix", 0.35f } } },

            { "ANGEL ARP", {
                { "arp_on", 1.0f }, { "arp_mode", 1.0f }, { "arp_octaves", 3.0f }, { "arp_rate", 3.0f }, { "arp_gate", 0.5f },
                { "choir_attack", 0.01f }, { "choir_release", 0.6f }, { "choir_vowel", 1.0f }, { "master_voicetype", 1.0f },
                { "delay_mix", 0.28f }, { "delay_feedback", 0.45f }, { "reverb_mix", 0.4f }, { "reverb_size", 0.85f } } },

            { "TREM VOICES", {
                { "arp_on", 1.0f }, { "arp_mode", 0.0f }, { "arp_octaves", 2.0f }, { "arp_rate", 4.0f }, { "arp_gate", 0.45f },
                { "vsyn_on", 1.0f }, { "vsyn_vowel", 3.0f }, { "vsyn_attack", 0.005f }, { "vsyn_release", 0.12f },
                { "choir_level", 0.5f }, { "choir_attack", 0.01f }, { "choir_release", 0.2f }, { "reverb_mix", 0.3f } } },

            { "DEEP VOICE", {
                { "chg_on", 1.0f }, { "chg_amount", 0.8f }, { "master_voicetype", 0.0f }, { "tune_on", 0.0f },
                { "choir_on", 0.0f }, { "chorus_mix", 0.0f }, { "delay_mix", 0.0f }, { "reverb_mix", 0.12f } } },

            { "ANIME GIRL", {
                { "chg_on", 1.0f }, { "chg_amount", 1.0f }, { "chg_pitch", 3.0f }, { "chg_formant", 1.0f },
                { "master_voicetype", 1.0f }, { "tune_scale", 1.0f }, { "tune_retune", 0.03f },
                { "choir_on", 0.0f }, { "chorus_mix", 0.15f }, { "reverb_mix", 0.18f } } },

            { "DUET DOUBLE", {
                { "chg_on", 1.0f }, { "chg_amount", 0.45f }, { "chg_width", 1.0f }, { "master_voicetype", 2.0f },
                { "tune_scale", 1.0f }, { "choir_on", 0.0f }, { "chorus_mix", 0.25f }, { "reverb_mix", 0.25f } } },
        };
        return patches;
    }
}
