# Voices - project notes (for contributors and for resuming work)

Last updated: 2026-11-18, version 0.1.0. License: AGPL-3.0-or-later, copyright LexLongcoding
(JUCE used under its AGPLv3 option). Sister project: Decu6000 (same look, fonts, patch system).

## What it is
Vocal synth / choir / vocoder / auto-tune / voice changer, VST3 + Standalone, Windows + Linux,
JUCE 8.0.4 (fetched by CMake). Owner's brief: lush and vibrant; auto-tune from subtle to heavy
stepped hip-hop; vocal synth with vowel selection; choir like classic Yamaha keyboard oohs/aahs;
easy voice changer; male / female / mixed voices; arpeggiator (trem, up, down, random, 1-4 octaves);
chorus / reverb / delay; hear results live before recording; keep the Decu6000 fonts and colours.

## Design decisions (and why)
- **Two plug-ins from one codebase** (`VOICES_FX` define). An instrument can't sit on a vocal
  track and an effect can't be played from a MIDI track in every DAW, so: `Voices` (instrument,
  side-chain "Voice In") and `Voices FX` (insert effect that also accepts MIDI).
  The Standalone is built from the instrument; there the *main* input is enabled (microphone).
- **Latency**: the voice path (PSOLA) has a fixed 12 ms latency. Voices FX reports it and delays
  its synths by the same amount so everything lines up after host compensation. The instrument
  reports 0 so played notes are instant (its side-chain voice is then 12 ms late - acceptable).
- **One engine, JUCE-free** (`Source/DSP/VoicesEngine.h`): unit-testable without JUCE, same code
  in the test and the plug-in.
- **Voice type is global** (header): choir sections, vocal-synth voice, vocoder colour and the
  voice-changer direction. The changer is then one AMOUNT knob ("straightforward/easy").
- **Auto-tune**: YIN detection (decimated, refined at full rate) -> scale/MIDI target with
  hysteresis -> one-pole correction with retune time (0 = instant steps) + humanize -> PSOLA.
  The same PSOLA pass applies the voice-changer pitch and formant shift; MIXED uses a second pass.
- **Vocoder AUTO**: with no keys held the carrier follows the *corrected* sung pitch.
- **Patches**: same PatchManager as Decu6000 (factory in code + XML user files), extension
  `.voicespatch`, folder "Voices Synth". Not exposed as host programs (Cubase program-0 issue).
- **Fonts**: the Decu Display (Orbitron-derived) + Share Tech Mono files, embedded via BinaryData.

## Source map
| File | Role |
|---|---|
| `Source/Parameters.h` | Table of all parameters (id, name, label, range, skew, default, unit, choice labels) |
| `Source/PluginProcessor.*` | Buses per build, MIDI (+ sustain), transport, read-outs for the editor, state |
| `Source/DSP/VoicesEngine.h` | Whole signal path: voice path, MIDI/arp routing, synths, FX, master |
| `Source/DSP/PitchDetector.h` | YIN pitch tracker |
| `Source/DSP/PitchShifter.h` | TD-PSOLA with formant control |
| `Source/DSP/PitchCorrector.h` | Scales + auto-tune correction logic |
| `Source/DSP/ChoirSynth.h` | Ensemble choir (+ shared PolyVoices allocator) |
| `Source/DSP/VocalSynth.h` | Solo vowel voice |
| `Source/DSP/Vocoder.h` | Channel vocoder + carrier synth |
| `Source/DSP/Arpeggiator.h` | Arp (host-synced / free) |
| `Source/DSP/Effects.h` | Chorus, ping-pong delay, FDN reverb |
| `Source/DSP/Formants.h` | Vowel tables (tenor / alto) and formant bank |
| `Source/UI/Theme.*` | Colours, fonts, knob drawing (from Decu6000) |
| `Source/UI/Widgets.*` | Knob, toggles, choice buttons/combos, meters, Panel base |
| `Source/UI/Panels.*` | The seven panels, pitch display, keyboard strip |
| `Source/PluginEditor.*` | Header (voice type, patch LCD, input badge), 1440x900 scaling canvas |
| `Tests/engine_render_test.cpp` | DSP test without JUCE, writes WAVs |
| `Tests/plugin_test.cpp` | Processor, patches, state, editor screenshots, VST3 host test |
| `docs/manual/make_manual.py` | Builds `docs/Voices-User-Manual.pdf` (reads Parameters.h) |

## Status at 2026-11-18
- Linux: VST3 (both) + Standalone build; DSP tests and plug-in tests pass; VST3s load and play
  in a JUCE host; editor screenshots in `docs/images`. Not yet tried with a real microphone or DAW.
- Windows: workflow ready (same as Decu6000's, which builds on GitHub Actions); not yet run.

## Possible next steps
- Try both plug-ins in a real DAW with a microphone (FL Studio / Ableton / Reaper).
- Optional: harmonizer (choir follows the sung pitch), formant-preserving octave doubler,
  MIDI learn (port from Decu6000), macOS build in CI.
