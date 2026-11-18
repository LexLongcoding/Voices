// Voices - Copyright (C) 2026 LexLongcoding
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
#include <array>

// Single source of truth for every Voices parameter. The processor builds its
// APVTS layout from this table; the editor uses it for knob labels; patches
// store values by these IDs (so old patches keep loading in new versions).
namespace Params
{
    enum class Unit { Percent, Hz, Time, Semitones, Integer, Toggle, Choice, Vowel };

    enum Choices { noChoices = 0, voiceTypes, keys, scales, tuneSources, carriers, arpModes, arpRates, delayTimes };

    struct Spec
    {
        const char* id;      // APVTS parameter ID
        const char* name;    // full name shown to the host
        const char* knob;    // short knob label
        float min, max;
        float centre;        // skew centre; <= 0 means linear
        float def;
        Unit unit;
        int choices = noChoices;
    };

    // Option labels for Unit::Choice parameters (value = index).
    inline const char* const* choiceLabels (int set, int& count)
    {
        static const char* const voiceTypeL[] = { "MALE", "FEMALE", "MIXED" };
        static const char* const keyL[]       = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        static const char* const scaleL[]     = { "CHROMATIC", "MAJOR", "MINOR", "HARM MINOR", "MAJ PENTA", "MIN PENTA",
                                                  "BLUES", "DORIAN", "MIXOLYDIAN", "PHRYGIAN" };
        static const char* const sourceL[]    = { "SCALE", "MIDI" };
        static const char* const carrierL[]   = { "SAW", "PULSE", "SUPER" };
        static const char* const arpModeL[]   = { "TREM", "UP", "DOWN", "RANDOM" };
        static const char* const arpRateL[]   = { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" };
        static const char* const delayL[]     = { "1/16", "1/8T", "1/8", "1/8D", "1/4T", "1/4", "1/4D", "1/2" };
        switch (set)
        {
            case voiceTypes:  count = 3;  return voiceTypeL;
            case keys:        count = 12; return keyL;
            case scales:      count = 10; return scaleL;
            case tuneSources: count = 2;  return sourceL;
            case carriers:    count = 3;  return carrierL;
            case arpModes:    count = 4;  return arpModeL;
            case arpRates:    count = 6;  return arpRateL;
            case delayTimes:  count = 8;  return delayL;
            default:          count = 0;  return nullptr;
        }
    }

    // Beat lengths for the tempo choices.
    constexpr double arpRateBeats[]   = { 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
    constexpr double delayTimeBeats[] = { 0.25, 1.0 / 3.0, 0.5, 0.75, 2.0 / 3.0, 1.0, 1.5, 2.0 };

    // Groups - the ID prefix before the first '_'.
    struct Group { const char* prefix; const char* title; };
    constexpr std::array<Group, 11> groups {{
        { "master", "MASTER" }, { "voice", "VOICE" }, { "tune", "AUTO-TUNE" }, { "chg", "VOICE CHANGER" },
        { "voc", "VOCODER" }, { "choir", "CHOIR" }, { "vsyn", "VOCAL SYNTH" }, { "arp", "ARP" },
        { "chorus", "CHORUS" }, { "delay", "DELAY" }, { "reverb", "REVERB" } }};

    using U = Unit;
    constexpr Spec specs[] = {
        { "master_level",   "Master Level",        "MASTER",   0.0f, 1.0f, 0.0f, 0.8f, U::Percent },
        { "master_voicetype","Voice Type",         "VOICES",   0.0f, 2.0f, 0.0f, 2.0f, U::Choice, voiceTypes },

        // Input voice path (audio in -> auto-tune / voice changer)
        { "voice_level",    "Voice Level",         "VOICE",    0.0f, 1.0f, 0.0f, 0.8f, U::Percent },

        { "tune_on",        "Auto-Tune On",        "ON",       0.0f, 1.0f, 0.0f, 1.0f, U::Toggle },
        { "tune_key",       "Auto-Tune Key",       "KEY",      0.0f, 11.0f, 0.0f, 0.0f, U::Choice, keys },
        { "tune_scale",     "Auto-Tune Scale",     "SCALE",    0.0f, 9.0f, 0.0f, 0.0f, U::Choice, scales },
        { "tune_source",    "Auto-Tune Notes From","SOURCE",   0.0f, 1.0f, 0.0f, 0.0f, U::Choice, tuneSources },
        { "tune_retune",    "Auto-Tune Retune Speed","RETUNE", 0.0f, 0.4f, 0.06f, 0.05f, U::Time },
        { "tune_amount",    "Auto-Tune Amount",    "AMOUNT",   0.0f, 1.0f, 0.0f, 1.0f, U::Percent },
        { "tune_humanize",  "Auto-Tune Humanize",  "HUMANIZE", 0.0f, 1.0f, 0.0f, 0.3f, U::Percent },

        { "chg_on",         "Voice Changer On",    "ON",       0.0f, 1.0f, 0.0f, 0.0f, U::Toggle },
        { "chg_amount",     "Voice Changer Amount","AMOUNT",   0.0f, 1.0f, 0.0f, 0.6f, U::Percent },
        { "chg_pitch",      "Voice Changer Pitch", "PITCH",   -12.0f, 12.0f, 0.0f, 0.0f, U::Semitones },
        { "chg_formant",    "Voice Changer Formant","FORMANT", -6.0f, 6.0f, 0.0f, 0.0f, U::Semitones },
        { "chg_width",      "Voice Changer Width", "WIDTH",    0.0f, 1.0f, 0.0f, 0.7f, U::Percent },

        { "voc_on",         "Vocoder On",          "ON",       0.0f, 1.0f, 0.0f, 0.0f, U::Toggle },
        { "voc_bands",      "Vocoder Bands",       "BANDS",    8.0f, 32.0f, 0.0f, 20.0f, U::Integer },
        { "voc_carrier",    "Vocoder Carrier",     "CARRIER",  0.0f, 2.0f, 0.0f, 2.0f, U::Choice, carriers },
        { "voc_release",    "Vocoder Release",     "RELEASE",  0.01f, 0.4f, 0.08f, 0.06f, U::Time },
        { "voc_formant",    "Vocoder Formant",     "FORMANT", -12.0f, 12.0f, 0.0f, 0.0f, U::Semitones },
        { "voc_sibilance",  "Vocoder Sibilance",   "SIBILANCE",0.0f, 1.0f, 0.0f, 0.4f, U::Percent },
        { "voc_auto",       "Vocoder Auto Pitch",  "AUTO",     0.0f, 1.0f, 0.0f, 1.0f, U::Toggle },
        { "voc_level",      "Vocoder Level",       "LEVEL",    0.0f, 1.0f, 0.0f, 0.8f, U::Percent },

        // Synth voices (MIDI)
        { "choir_on",       "Choir On",            "ON",       0.0f, 1.0f, 0.0f, 1.0f, U::Toggle },
        { "choir_vowel",    "Choir Vowel (Ooh-Aah)","OOH / AAH",0.0f, 1.0f, 0.0f, 1.0f, U::Percent },
        { "choir_size",     "Choir Ensemble",      "ENSEMBLE", 0.0f, 1.0f, 0.0f, 0.6f, U::Percent },
        { "choir_breath",   "Choir Breath",        "BREATH",   0.0f, 1.0f, 0.0f, 0.15f, U::Percent },
        { "choir_vibrato",  "Choir Vibrato",       "VIBRATO",  0.0f, 1.0f, 0.0f, 0.3f, U::Percent },
        { "choir_attack",   "Choir Attack",        "ATTACK",   0.005f, 3.0f, 0.3f, 0.25f, U::Time },
        { "choir_release",  "Choir Release",       "RELEASE",  0.02f, 5.0f, 0.6f, 0.8f, U::Time },
        { "choir_level",    "Choir Level",         "LEVEL",    0.0f, 1.0f, 0.0f, 0.75f, U::Percent },

        { "vsyn_on",        "Vocal Synth On",      "ON",       0.0f, 1.0f, 0.0f, 0.0f, U::Toggle },
        { "vsyn_vowel",     "Vocal Synth Vowel",   "VOWEL",    0.0f, 4.0f, 0.0f, 0.0f, U::Vowel },
        { "vsyn_vibrato",   "Vocal Synth Vibrato", "VIBRATO",  0.0f, 1.0f, 0.0f, 0.4f, U::Percent },
        { "vsyn_breath",    "Vocal Synth Breath",  "BREATH",   0.0f, 1.0f, 0.0f, 0.1f, U::Percent },
        { "vsyn_bright",    "Vocal Synth Brightness","BRIGHT", 0.0f, 1.0f, 0.0f, 0.5f, U::Percent },
        { "vsyn_glide",     "Vocal Synth Glide",   "GLIDE",    0.0f, 0.5f, 0.08f, 0.03f, U::Time },
        { "vsyn_attack",    "Vocal Synth Attack",  "ATTACK",   0.003f, 2.0f, 0.2f, 0.04f, U::Time },
        { "vsyn_release",   "Vocal Synth Release", "RELEASE",  0.02f, 4.0f, 0.5f, 0.35f, U::Time },
        { "vsyn_level",     "Vocal Synth Level",   "LEVEL",    0.0f, 1.0f, 0.0f, 0.75f, U::Percent },

        { "arp_on",         "Arpeggiator On",      "ON",       0.0f, 1.0f, 0.0f, 0.0f, U::Toggle },
        { "arp_mode",       "Arpeggiator Mode",    "MODE",     0.0f, 3.0f, 0.0f, 1.0f, U::Choice, arpModes },
        { "arp_octaves",    "Arpeggiator Octaves", "OCTAVES",  1.0f, 4.0f, 0.0f, 1.0f, U::Integer },
        { "arp_rate",       "Arpeggiator Rate",    "RATE",     0.0f, 5.0f, 0.0f, 3.0f, U::Choice, arpRates },
        { "arp_gate",       "Arpeggiator Gate",    "GATE",     0.05f, 1.0f, 0.0f, 0.6f, U::Percent },
        { "arp_latch",      "Arpeggiator Latch",   "LATCH",    0.0f, 1.0f, 0.0f, 0.0f, U::Toggle },

        // FX
        { "chorus_rate",    "Chorus Rate",         "RATE",     0.05f, 3.0f, 0.6f, 0.6f, U::Hz },
        { "chorus_depth",   "Chorus Depth",        "DEPTH",    0.0f, 1.0f, 0.0f, 0.5f, U::Percent },
        { "chorus_mix",     "Chorus Mix",          "MIX",      0.0f, 1.0f, 0.0f, 0.3f, U::Percent },
        { "delay_time",     "Delay Time",          "TIME",     0.0f, 7.0f, 0.0f, 3.0f, U::Choice, delayTimes },
        { "delay_feedback", "Delay Feedback",      "FDBK",     0.0f, 0.92f, 0.0f, 0.35f, U::Percent },
        { "delay_mix",      "Delay Mix",           "MIX",      0.0f, 1.0f, 0.0f, 0.15f, U::Percent },
        { "reverb_size",    "Reverb Size",         "SIZE",     0.0f, 1.0f, 0.0f, 0.65f, U::Percent },
        { "reverb_damp",    "Reverb Damping",      "DAMP",     0.0f, 1.0f, 0.0f, 0.45f, U::Percent },
        { "reverb_mix",     "Reverb Mix",          "MIX",      0.0f, 1.0f, 0.0f, 0.3f, U::Percent },
    };

    constexpr int numParams = static_cast<int> (sizeof (specs) / sizeof (specs[0]));
}
