// Voices - Copyright (C) 2026 LexLongcoding
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
#include "DspUtils.h"
#include "PitchDetector.h"
#include "PitchShifter.h"
#include "PitchCorrector.h"
#include "ChoirSynth.h"
#include "VocalSynth.h"
#include "Vocoder.h"
#include "Arpeggiator.h"
#include "Effects.h"
#include <array>
#include <vector>

// The whole Voices signal path, independent of JUCE (the plug-in wraps it;
// Tests/engine_render_test.cpp drives it directly).
//
//   voice in --> pitch detect --> AUTO-TUNE + VOICE CHANGER (PSOLA) --> voice bus --+
//          \--------------------------------> VOCODER (modulator) ----------------+--> CHORUS -> DELAY -> REVERB -> out
//   MIDI --> [ARP] --> CHOIR, VOCAL SYNTH, VOCODER carrier --------> synth bus -----+
class VoicesEngine
{
public:
    struct Settings
    {
        Formants::VoiceType voiceType = Formants::VoiceType::mixed;
        float masterLevel = 0.8f, voiceLevel = 0.8f;

        bool  tuneOn = true;
        int   tuneKey = 0, tuneScale = 0;
        bool  tuneFromMidi = false;
        float retune = 0.05f, amount = 1.0f, humanize = 0.3f;

        bool  changerOn = false;
        float changerAmount = 0.6f, changerPitch = 0.0f, changerFormant = 0.0f, changerWidth = 0.7f;

        bool  vocoderOn = false, vocoderAuto = true;
        float vocoderLevel = 0.8f;
        Vocoder::Settings vocoder;

        bool  choirOn = true;
        float choirLevel = 0.75f;
        ChoirSynth::Settings choir;

        bool  vocalOn = false;
        float vocalLevel = 0.75f;
        VocalSynth::Settings vocal;

        bool  arpOn = false;
        Arpeggiator::Settings arp;

        float chorusRate = 0.6f, chorusDepth = 0.5f, chorusMix = 0.3f;
        double delayBeats = 0.75; float delayFeedback = 0.35f, delayMix = 0.15f;
        float reverbSize = 0.65f, reverbDamp = 0.45f, reverbMix = 0.3f;
    };

    struct MidiEvent { int pos; enum Type { noteOn, noteOff, allOff } type; int note; float velocity; };
    struct Transport { double bpm = 120.0; bool playing = false; double ppq = 0.0; };

    // alignSynthToVoice: delay the synths by the pitch-shifter latency so everything
    // lines up when the host compensates the reported latency (used by Voices FX).
    void prepare (double sr, bool alignSynthToVoice)
    {
        sampleRate = sr;
        align = alignSynthToVoice;
        detector.prepare (sr);
        shiftA.prepare (sr);
        shiftB.prepare (sr);
        latency = shiftA.getLatency();
        dryDelay.prepare (latency + 8);
        synthDelayL.prepare (latency + 8);
        synthDelayR.prepare (latency + 8);
        choir.prepare (sr);
        vocal.prepare (sr);
        vocoder.prepare (sr);
        arp.prepare (sr);
        chorus.prepare (sr);
        delay.prepare (sr);
        reverb.prepare (sr);
        for (auto* b : { &mono, &voiceL, &voiceR, &synthL, &synthR })
            b->assign (static_cast<size_t> (chunk), 0.0f);
        reset();
    }

    void reset()
    {
        detector.reset(); shiftA.reset(); shiftB.reset(); corrector.reset();
        dryDelay.reset(); synthDelayL.reset(); synthDelayR.reset();
        choir.reset(); vocal.reset(); vocoder.reset(); arp.reset();
        chorus.reset(); delay.reset(); reverb.reset();
        rawHeld.fill (false);
        wet = 0.0f; vocoderGain = 0.0f; samplesSinceDetect = 0;
        lastArpOn = settings.arpOn;
        lastChoirOn = settings.choirOn; lastVocalOn = settings.vocalOn; lastVocoderOn = settings.vocoderOn;
        period = 0.0f; voiced = false; correction = 0.0f;
    }

    int getLatency() const { return latency; }   // voice path latency in samples

    void setSettings (const Settings& s)
    {
        settings = s;
        auto c = s.choir;   c.type = s.voiceType;  choir.setSettings (c);
        auto v = s.vocal;   v.type = s.voiceType;  vocal.setSettings (v);
        auto vc = s.vocoder; vc.type = s.voiceType; vocoder.setSettings (vc);
        arp.setLatch (s.arp.latch);
        arp.setSettings (s.arp);
    }

    // in: nullptr / 0 channels for no input. out: stereo, overwritten.
    void process (const float* const* in, int numIn, float* outL, float* outR, int numSamples,
                  const MidiEvent* events, int numEvents, const Transport& tp)
    {
        int ev = 0;
        for (int start = 0; start < numSamples; start += chunk)
        {
            const int n = std::min (chunk, numSamples - start);
            // events for this chunk (they arrive sorted by position)
            const int evStart = ev;
            while (ev < numEvents && events[ev].pos < start + n) ++ev;

            Transport t = tp;
            t.ppq = tp.ppq + static_cast<double> (start) / sampleRate * tp.bpm / 60.0;
            processChunk (in, numIn, start, outL + start, outR + start, n, events + evStart, ev - evStart, t);
        }
        // leftover events past the end (bad host data): apply note-offs so nothing hangs
        for (; ev < numEvents; ++ev)
            if (events[ev].type != MidiEvent::noteOn) handleSynthEvent (events[ev]);
    }

    //==========================================================================
    // Read-outs for the editor (call from the audio thread, then publish).
    bool  isVoiced() const        { return voiced; }
    float getInputMidi() const    { return voiced ? Dsp::hzToMidi (static_cast<float> (sampleRate) / period) : 0.0f; }
    float getOutputMidi() const   { return voiced ? getInputMidi() + (settings.tuneOn ? correction : 0.0f) : 0.0f; }
    int   getTargetNote() const   { return corrector.getTarget(); }
    int   getArpStep() const      { return arp.getStepDisplay(); }
    const Vocoder& getVocoder() const { return vocoder; }

    // Gender / character shift used by the voice changer, in semitones.
    static void changerShift (const Settings& s, bool upper, float& pitchSt, float& formantSt)
    {
        const float g = s.changerAmount;
        float dir = 0.0f;
        switch (s.voiceType)
        {
            case Formants::VoiceType::male:   dir = -1.0f; break;
            case Formants::VoiceType::female: dir = 1.0f; break;
            case Formants::VoiceType::mixed:  dir = upper ? 1.0f : -1.0f; break;
        }
        pitchSt = dir * g * 6.0f + s.changerPitch;
        formantSt = dir * g * 3.0f + s.changerFormant;
    }

private:
    void processChunk (const float* const* in, int numIn, int offset, float* outL, float* outR, int n,
                       const MidiEvent* events, int numEvents, const Transport& tp)
    {
        // ---- input to mono
        for (int i = 0; i < n; ++i)
        {
            float x = 0.0f;
            for (int c = 0; c < numIn; ++c) x += in[c][offset + i];
            mono[static_cast<size_t> (i)] = numIn > 0 ? x / static_cast<float> (numIn) : 0.0f;
        }

        // ---- engine on/off transitions
        if (settings.arpOn != lastArpOn)
        {
            synthAllOff();
            arp.reset();
            if (settings.arpOn)   // carry keys already held into the arp
                for (int k = 0; k < 128; ++k) if (rawHeld[static_cast<size_t> (k)]) arp.keyOn (k, 0.8f, 0);
            lastArpOn = settings.arpOn;
        }
        if (settings.choirOn != lastChoirOn)     { if (! settings.choirOn) choir.allNotesOff(); lastChoirOn = settings.choirOn; }
        if (settings.vocalOn != lastVocalOn)     { if (! settings.vocalOn) vocal.allNotesOff(); lastVocalOn = settings.vocalOn; }
        if (settings.vocoderOn != lastVocoderOn) { if (! settings.vocoderOn) vocoder.allNotesOff(); lastVocoderOn = settings.vocoderOn; }

        // ---- MIDI -> arp or straight to the synths
        int numSynth = 0;
        for (int e = 0; e < numEvents; ++e)
        {
            auto m = events[e];
            m.pos = std::clamp (m.pos - offset, 0, n - 1);
            if (m.type == MidiEvent::noteOn)  rawHeld[static_cast<size_t> (m.note & 127)] = true;
            if (m.type == MidiEvent::noteOff) rawHeld[static_cast<size_t> (m.note & 127)] = false;
            if (m.type == MidiEvent::allOff)  { rawHeld.fill (false); arp.reset(); }

            if (settings.arpOn && m.type != MidiEvent::allOff)
            {
                if (m.type == MidiEvent::noteOn) arp.keyOn (m.note, m.velocity, m.pos);
                else                             arp.keyOff (m.note);
            }
            else if (numSynth < maxSynthEvents)
                synthEvents[static_cast<size_t> (numSynth++)] = m;
        }
        if (settings.arpOn)
        {
            const int count = arp.process (n, std::max (20.0, tp.bpm), tp.playing, tp.ppq, arpEvents.data());
            for (int k = 0; k < count && numSynth < maxSynthEvents; ++k)
            {
                const auto& a = arpEvents[static_cast<size_t> (k)];
                synthEvents[static_cast<size_t> (numSynth++)] = { a.pos, a.on ? MidiEvent::noteOn : MidiEvent::noteOff, a.note, a.velocity };
            }
            // merge order: stable sort by position (note-offs first at equal positions)
            std::stable_sort (synthEvents.begin(), synthEvents.begin() + numSynth, [] (const MidiEvent& a, const MidiEvent& b)
            {
                if (a.pos != b.pos) return a.pos < b.pos;
                return a.type == MidiEvent::noteOff && b.type != MidiEvent::noteOff;
            });
        }

        // ---- voice path (sample by sample: detection -> correction -> PSOLA)
        processVoice (n);

        // ---- synths, split at event positions
        std::fill (synthL.begin(), synthL.begin() + n, 0.0f);
        std::fill (synthR.begin(), synthR.begin() + n, 0.0f);
        vocoder.setAutoPitch (settings.vocoderAuto, voiced, getOutputMidi());
        int pos = 0;
        for (int e = 0; e <= numSynth; ++e)
        {
            const int end = e < numSynth ? synthEvents[static_cast<size_t> (e)].pos : n;
            if (end > pos) { renderSynths (pos, end); pos = end; }
            if (e < numSynth) handleSynthEvent (synthEvents[static_cast<size_t> (e)]);
        }

        // ---- mix: voice bus + (aligned) synth bus
        for (int i = 0; i < n; ++i)
        {
            float sl = synthL[static_cast<size_t> (i)], sr = synthR[static_cast<size_t> (i)];
            if (align)
            {
                synthDelayL.push (sl); synthDelayR.push (sr);
                sl = synthDelayL.readInt (latency + 1); sr = synthDelayR.readInt (latency + 1);
            }
            outL[i] = voiceL[static_cast<size_t> (i)] * settings.voiceLevel + sl;
            outR[i] = voiceR[static_cast<size_t> (i)] * settings.voiceLevel + sr;
        }

        // ---- FX and master
        chorus.process (outL, outR, n, settings.chorusRate, settings.chorusDepth, settings.chorusMix);
        const float delaySec = static_cast<float> (settings.delayBeats * 60.0 / std::max (20.0, tp.bpm));
        delay.process (outL, outR, n, delaySec, settings.delayFeedback, settings.delayMix);
        reverb.process (outL, outR, n, settings.reverbSize, settings.reverbDamp, settings.reverbMix);
        for (int i = 0; i < n; ++i)
        {
            outL[i] = Dsp::softLimit (outL[i] * settings.masterLevel);
            outR[i] = Dsp::softLimit (outR[i] * settings.masterLevel);
        }
    }

    void processVoice (int n)
    {
        const auto& s = settings;
        const bool mixedChanger = s.changerOn && s.voiceType == Formants::VoiceType::mixed;
        const float wetTarget = (s.tuneOn || s.changerOn) ? 1.0f : 0.0f;
        const float fade = 1.0f - Dsp::smoothCoeff (0.01f, sampleRate);

        PitchCorrector::Settings cs;
        cs.retuneSec = s.retune;
        cs.amount = s.amount;
        cs.humanize = s.humanize;
        cs.pcMask = Scales::inKey (Scales::all[static_cast<size_t> (std::clamp (s.tuneScale, 0, 9))].mask, s.tuneKey);
        if (s.tuneFromMidi)
        {
            uint16_t m = 0;
            for (int k = 0; k < 128; ++k) if (rawHeld[static_cast<size_t> (k)]) m |= static_cast<uint16_t> (1u << (k % 12));
            if (m != 0) cs.pcMask = m;   // no keys held: fall back to the scale
        }

        float pA = 0.0f, fA = 0.0f, pB = 0.0f, fB = 0.0f;
        if (s.changerOn)
        {
            changerShift (s, true, pA, fA);
            changerShift (s, false, pB, fB);
        }
        float panAL = 1.0f, panAR = 1.0f, panBL = 0.0f, panBR = 0.0f;
        if (mixedChanger)
        {
            Dsp::panGains (-s.changerWidth, panAL, panAR);
            Dsp::panGains (s.changerWidth, panBL, panBR);
            panAL *= 0.85f; panAR *= 0.85f; panBL *= 0.85f; panBR *= 0.85f;
        }
        else
        {
            panAL = panAR = 1.0f;
        }

        auto applyControl = [&]
        {
            const float corr = s.tuneOn ? correction : 0.0f;
            const float per = voiced ? period : 0.0f;
            shiftA.setControl (per, Dsp::semisToRatio (corr + pA), Dsp::semisToRatio (fA));
            shiftB.setControl (per, Dsp::semisToRatio (corr + pB), Dsp::semisToRatio (fB));
        };
        applyControl();

        for (int i = 0; i < n; ++i)
        {
            const float x = mono[static_cast<size_t> (i)];
            ++samplesSinceDetect;
            if (detector.push (x))
            {
                voiced = detector.isVoiced();
                period = detector.getPeriod();
                const float dt = static_cast<float> (samplesSinceDetect) / static_cast<float> (sampleRate);
                samplesSinceDetect = 0;
                correction = corrector.update (voiced, voiced ? Dsp::hzToMidi (detector.getFrequency()) : 0.0f, cs, dt);
                applyControl();
            }

            dryDelay.push (x);
            const float dry = dryDelay.readInt (latency + 1);   // newest sample is delay 1
            const float a = shiftA.process (x);
            const float b = shiftB.process (x);   // kept running so MIXED switches in without a glitch
            wet += (wetTarget - wet) * fade;

            float l, r;
            if (mixedChanger) { l = a * panAL + b * panBL; r = a * panAR + b * panBR; }
            else              { l = r = a; }
            voiceL[static_cast<size_t> (i)] = dry * (1.0f - wet) + l * wet;
            voiceR[static_cast<size_t> (i)] = dry * (1.0f - wet) + r * wet;
        }
    }

    void renderSynths (int a, int b)
    {
        const int len = b - a;
        float* l = synthL.data() + a;
        float* r = synthR.data() + a;

        // render each engine into scratch, then mix at its level
        std::array<float, chunk> tl {}, tr {};
        choir.render (tl.data(), tr.data(), len);
        for (int i = 0; i < len; ++i) { l[i] += tl[static_cast<size_t> (i)] * settings.choirLevel; r[i] += tr[static_cast<size_t> (i)] * settings.choirLevel; }

        std::fill (tl.begin(), tl.begin() + len, 0.0f); std::fill (tr.begin(), tr.begin() + len, 0.0f);
        vocal.render (tl.data(), tr.data(), len);
        for (int i = 0; i < len; ++i) { l[i] += tl[static_cast<size_t> (i)] * settings.vocalLevel; r[i] += tr[static_cast<size_t> (i)] * settings.vocalLevel; }

        const float vTarget = settings.vocoderOn ? settings.vocoderLevel : 0.0f;
        if (vTarget > 0.0f || vocoderGain > 0.0001f)
        {
            std::fill (tl.begin(), tl.begin() + len, 0.0f); std::fill (tr.begin(), tr.begin() + len, 0.0f);
            vocoder.render (mono.data() + a, tl.data(), tr.data(), len);
            const float k = 1.0f - Dsp::smoothCoeff (0.015f, sampleRate);
            for (int i = 0; i < len; ++i)
            {
                vocoderGain += (vTarget - vocoderGain) * k;
                l[i] += tl[static_cast<size_t> (i)] * vocoderGain;
                r[i] += tr[static_cast<size_t> (i)] * vocoderGain;
            }
        }
    }

    void handleSynthEvent (const MidiEvent& m)
    {
        switch (m.type)
        {
            case MidiEvent::noteOn:
                if (settings.choirOn)   choir.noteOn (m.note, m.velocity);
                if (settings.vocalOn)   vocal.noteOn (m.note, m.velocity);
                if (settings.vocoderOn) vocoder.noteOn (m.note, m.velocity);
                break;
            case MidiEvent::noteOff:
                choir.noteOff (m.note); vocal.noteOff (m.note); vocoder.noteOff (m.note);
                break;
            case MidiEvent::allOff:
                synthAllOff();
                break;
        }
    }

    void synthAllOff() { choir.allNotesOff(); vocal.allNotesOff(); vocoder.allNotesOff(); }

    static constexpr int chunk = 256;
    static constexpr int maxSynthEvents = 256;

    double sampleRate = 48000.0;
    bool align = false;
    int latency = 0;
    Settings settings;

    PitchDetector detector;
    PitchCorrector corrector;
    PitchShifter shiftA, shiftB;
    Fx::DelayLine dryDelay, synthDelayL, synthDelayR;
    ChoirSynth choir;
    VocalSynth vocal;
    Vocoder vocoder;
    Arpeggiator arp;
    Fx::Chorus chorus;
    Fx::PingPongDelay delay;
    Fx::Reverb reverb;

    std::vector<float> mono, voiceL, voiceR, synthL, synthR;
    std::array<MidiEvent, maxSynthEvents> synthEvents {};
    std::array<Arpeggiator::Event, Arpeggiator::maxEvents> arpEvents {};
    std::array<bool, 128> rawHeld {};

    float wet = 0.0f, vocoderGain = 0.0f, period = 0.0f, correction = 0.0f;
    bool voiced = false;
    int samplesSinceDetect = 0;
    bool lastArpOn = false, lastChoirOn = true, lastVocalOn = false, lastVocoderOn = false;
};
