#!/usr/bin/env python3
# Voices - Copyright (C) 2026 LexLongcoding
# SPDX-License-Identifier: AGPL-3.0-or-later
"""
Builds docs/Voices-User-Manual.pdf.

Control ranges and defaults are read straight from Source/Parameters.h, so the
manual stays in step with the plugin. Requires: pip install reportlab
Run from anywhere:  python3 docs/manual/make_manual.py
"""
import os, re
from reportlab.lib.pagesizes import letter
from reportlab.lib.units import inch
from reportlab.lib import colors
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.enums import TA_LEFT, TA_CENTER
from reportlab.platypus import (BaseDocTemplate, PageTemplate, Frame, Paragraph, Spacer, Table,
                                TableStyle, Image, PageBreak, KeepTogether, NextPageTemplate, CondPageBreak)
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "docs", "Voices-User-Manual.pdf")
IMG = os.path.join(ROOT, "docs", "images")
VERSION = re.search(r"project\(Voices VERSION ([0-9.]+)\)", open(os.path.join(ROOT, "CMakeLists.txt")).read()).group(1)

# ---------------------------------------------------------------- fonts & colours
FONTS = os.path.join(ROOT, "Resources", "Fonts")
pdfmetrics.registerFont(TTFont("DecuDisplay", os.path.join(FONTS, "DecuDisplay-Bold.ttf")))
pdfmetrics.registerFont(TTFont("DecuBlack", os.path.join(FONTS, "DecuDisplay-Black.ttf")))
pdfmetrics.registerFont(TTFont("DecuMono", os.path.join(FONTS, "ShareTechMono-Regular.ttf")))

BG     = colors.HexColor("#12102B")
PANEL  = colors.HexColor("#2A2150")
ORANGE = colors.HexColor("#FF7A1A")
PINK   = colors.HexColor("#FF2E93")
CYAN   = colors.HexColor("#3DE8FF")
INK    = colors.HexColor("#1D1838")
SOFT   = colors.HexColor("#F3F1FB")
RULE   = colors.HexColor("#D8D2FF")
MUTED  = colors.HexColor("#5E5885")

S = {
    "h1":   ParagraphStyle("h1", fontName="DecuDisplay", fontSize=20, leading=24, textColor=INK, spaceBefore=4, spaceAfter=10),
    "h2":   ParagraphStyle("h2", fontName="DecuDisplay", fontSize=13, leading=17, textColor=PINK, spaceBefore=12, spaceAfter=5),
    "h3":   ParagraphStyle("h3", fontName="Helvetica-Bold", fontSize=10.5, leading=14, textColor=INK, spaceBefore=8, spaceAfter=3),
    "body": ParagraphStyle("body", fontName="Helvetica", fontSize=9.6, leading=13.6, textColor=INK, spaceAfter=6),
    "bul":  ParagraphStyle("bul", fontName="Helvetica", fontSize=9.6, leading=13.4, textColor=INK, leftIndent=14,
                           bulletIndent=3, spaceAfter=2.5),
    "cell": ParagraphStyle("cell", fontName="Helvetica", fontSize=8.6, leading=11, textColor=INK),
    "cellb":ParagraphStyle("cellb", fontName="Helvetica-Bold", fontSize=8.6, leading=11, textColor=INK),
    "head": ParagraphStyle("head", fontName="DecuMono", fontSize=8.6, leading=11, textColor=colors.white),
    "cap":  ParagraphStyle("cap", fontName="Helvetica-Oblique", fontSize=8.4, leading=11, textColor=MUTED,
                           alignment=TA_CENTER, spaceBefore=3, spaceAfter=10),
    "note": ParagraphStyle("note", fontName="Helvetica", fontSize=9, leading=12.6, textColor=INK),
    "toc":  ParagraphStyle("toc", fontName="Helvetica", fontSize=10.5, leading=18, textColor=INK),
}

def P(t, s="body"): return Paragraph(t, S[s])
def bullets(items): return [Paragraph(i, S["bul"], bulletText="\u2022") for i in items]

def note(text, colour=CYAN):
    t = Table([[Paragraph(text, S["note"])]], colWidths=[6.5 * inch])
    t.setStyle(TableStyle([("BACKGROUND", (0, 0), (-1, -1), SOFT), ("LINEBEFORE", (0, 0), (0, -1), 3, colour),
                           ("LEFTPADDING", (0, 0), (-1, -1), 10), ("RIGHTPADDING", (0, 0), (-1, -1), 10),
                           ("TOPPADDING", (0, 0), (-1, -1), 7), ("BOTTOMPADDING", (0, 0), (-1, -1), 7)]))
    return t

def table(rows, widths, header=True, accent=PANEL):
    data = []
    for r, row in enumerate(rows):
        style = "head" if (header and r == 0) else "cell"
        data.append([c if not isinstance(c, str) else Paragraph(c, S[style]) for c in row])
    t = Table(data, colWidths=widths, repeatRows=1 if header else 0)
    st = [("VALIGN", (0, 0), (-1, -1), "TOP"), ("LINEBELOW", (0, 0), (-1, -1), 0.4, RULE),
          ("TOPPADDING", (0, 0), (-1, -1), 4), ("BOTTOMPADDING", (0, 0), (-1, -1), 4),
          ("LEFTPADDING", (0, 0), (-1, -1), 5), ("RIGHTPADDING", (0, 0), (-1, -1), 5)]
    if header:
        st += [("BACKGROUND", (0, 0), (-1, 0), accent)]
    for r in range(1 if header else 0, len(rows)):
        if r % 2 == 0:
            st.append(("BACKGROUND", (0, r), (-1, r), SOFT))
    t.setStyle(TableStyle(st))
    return t

def shot(name, width=6.5 * inch, caption=None):
    path = os.path.join(IMG, name)
    from reportlab.lib.utils import ImageReader
    iw, ih = ImageReader(path).getSize()
    img = Image(path, width=width, height=width * ih / iw)
    out = [img]
    if caption: out.append(P(caption, "cap"))
    return KeepTogether(out)

# ---------------------------------------------------------------- parameters from source
SPEC_RE = re.compile(r'\{\s*"(\w+)",\s*"([^"]+)",\s*"([^"]+)",\s*([-0-9.]+)f,\s*([-0-9.]+)f,\s*([-0-9.]+)f,\s*([-0-9.]+)f,\s*U::(\w+)\s*(?:,\s*(\w+))?\s*\}')
PARAMS_H = open(os.path.join(ROOT, "Source", "Parameters.h")).read()
SPECS = {m[0]: dict(id=m[0], name=m[1], knob=m[2], lo=float(m[3]), hi=float(m[4]), default=float(m[6]), unit=m[7], choices=m[8])
         for m in SPEC_RE.findall(PARAMS_H)}

def choice_labels(name):
    var = {"voiceTypes": "voiceTypeL", "keys": "keyL", "scales": "scaleL", "tuneSources": "sourceL", "carriers": "carrierL",
           "arpModes": "arpModeL", "arpRates": "arpRateL", "delayTimes": "delayL"}[name]
    body = re.search(var + r"\[\]\s*=\s*\{([^}]*)\}", PARAMS_H).group(1)
    return re.findall(r'"([^"]*)"', body)

def fmt(v, s):
    unit = s["unit"]
    if unit == "Percent":   return f"{round(v * 100)}%"
    if unit == "Hz":        return f"{v:.2f} Hz"
    if unit == "Time":      return "0 ms" if v <= 0.0005 else (f"{round(v * 1000)} ms" if v < 1 else f"{v:.2f} s".replace(".00 s", " s"))
    if unit == "Semitones": return f"{'+' if v > 0 else ''}{v:g} st"
    if unit == "Integer":   return str(round(v))
    if unit == "Toggle":    return "ON" if v >= 0.5 else "OFF"
    if unit == "Vowel":     return "AEIOU"[round(v)]
    if unit == "Choice":    return choice_labels(s["choices"])[round(v)]
    return str(v)

def rng(s):
    if s["unit"] == "Choice":
        labels = choice_labels(s["choices"])
        return " / ".join(labels) if len(labels) <= 4 else f"{labels[0]} ... {labels[-1]} ({len(labels)})"
    if s["unit"] == "Toggle": return "OFF / ON"
    if s["unit"] == "Vowel":  return "A E I O U (morphs)"
    return f"{fmt(s['lo'], s)} to {fmt(s['hi'], s)}"

def controls(prefix, desc, accent=PANEL):
    rows = [["CONTROL", "WHAT IT DOES", "RANGE", "DEFAULT"]]
    for pid, s in SPECS.items():
        if pid.split("_")[0] != prefix: continue
        suffix = pid.split("_", 1)[1]
        rows.append([f"<b>{s['knob']}</b>", desc.get(suffix, ""), rng(s), fmt(s["default"], s)])
    return table(rows, [0.95 * inch, 3.3 * inch, 1.45 * inch, 0.8 * inch], accent=accent)

# ---------------------------------------------------------------- page decoration
def tricolour(c, y, x0, bw, hgt):
    c.setFillColor(ORANGE); c.rect(x0, y, bw * 0.5, hgt, stroke=0, fill=1)
    c.setFillColor(PINK);   c.rect(x0 + bw * 0.5, y, bw * 0.333, hgt, stroke=0, fill=1)
    c.setFillColor(CYAN);   c.rect(x0 + bw * 0.833, y, bw * 0.167, hgt, stroke=0, fill=1)

def logo(c, x, y, size):
    c.setFont("DecuBlack", size)
    c.setFillColor(PINK);   c.drawString(x, y, "VOI")
    c.setFillColor(ORANGE); c.drawString(x + pdfmetrics.stringWidth("VOI", "DecuBlack", size), y, "CES")

def on_cover(c, doc):
    w, h = letter
    c.setFillColor(BG); c.rect(0, 0, w, h, stroke=0, fill=1)
    c.setFillColor(colors.Color(0.24, 0.91, 1.0, alpha=0.035))
    for y in range(0, int(h), 3): c.rect(0, y, w, 1, stroke=0, fill=1)
    tricolour(c, h - 3.05 * inch, 0.75 * inch, w - 1.5 * inch, 3)
    logo(c, 0.75 * inch, h - 2.2 * inch, 54)
    c.setFillColor(colors.HexColor("#D8D2FF")); c.setFont("DecuMono", 13)
    c.drawString(0.78 * inch, h - 2.65 * inch, "VOCAL / CHOIR SYNTHESIZER // VOCODER // AUTO-TUNE")
    c.setFont("DecuDisplay", 26); c.setFillColor(colors.white)
    c.drawString(0.75 * inch, h - 3.75 * inch, "USER MANUAL")
    c.setFont("DecuMono", 11); c.setFillColor(CYAN)
    c.drawString(0.78 * inch, h - 4.1 * inch, f"VERSION {VERSION}  //  VST3 + STANDALONE  //  WINDOWS + LINUX")
    img = os.path.join(IMG, "main_view.png")
    iw = w - 1.5 * inch; ih = iw * 900 / 1440
    c.drawImage(img, 0.75 * inch, 1.35 * inch, iw, ih)
    c.setStrokeColor(colors.HexColor("#3A3170")); c.setLineWidth(1); c.rect(0.75 * inch, 1.35 * inch, iw, ih)
    c.setFont("DecuMono", 9); c.setFillColor(colors.HexColor("#8A83B8"))
    c.drawString(0.78 * inch, 0.8 * inch, "FREE & OPEN SOURCE  //  GNU AGPL v3.0 OR LATER  //  BY LEXLONGCODING")

def on_page(c, doc):
    w, h = letter
    c.setFillColor(BG); c.rect(0, h - 0.42 * inch, w, 0.42 * inch, stroke=0, fill=1)
    logo(c, 0.75 * inch, h - 0.28 * inch, 11)
    c.setFont("DecuMono", 8.5); c.setFillColor(colors.HexColor("#D8D2FF"))
    c.drawRightString(w - 0.75 * inch, h - 0.28 * inch, "USER MANUAL  //  v" + VERSION)
    tricolour(c, h - 0.45 * inch, 0, w, 2)
    c.setFont("DecuMono", 8.5); c.setFillColor(MUTED)
    c.drawCentredString(w / 2, 0.45 * inch, str(doc.page))

# ---------------------------------------------------------------- content
story = [NextPageTemplate("body"), PageBreak()]
story += [P("Contents", "h1")]
toc = ["1  Welcome", "2  Installing", "3  Quick start: hear yourself live", "4  Routing in your DAW", "5  The header",
       "6  Auto-tune", "7  Voice changer", "8  Vocoder", "9  Choir", "10  Vocal synth", "11  Arpeggiator",
       "12  FX and master", "13  Patches", "14  Recipes", "15  Troubleshooting", "16  Credits and license"]
story += [P(t, "toc") for t in toc]
story += [Spacer(1, 14), note("<b>In a hurry?</b> Section 3 gets you singing through Voices in a couple of minutes; "
                              "section 4 shows where to put it in your DAW.", ORANGE), PageBreak()]

# 1
story += [P("1  Welcome", "h1"),
          P("Voices is a vocal instrument and a vocal effect in one: a lush choir in the style of classic keyboard "
            "\"oohs and aahs\", a solo vocal synth with vowel selection, a vocoder, auto-tune from subtle to hard and "
            "robotic, and an easy voice changer - all through chorus, delay and reverb, in the Decu6000 look."),
          P("Three ways to run it", "h2"),
          table([["FILE", "TYPE", "USE IT FOR"],
                 ["<b>Voices.vst3</b>", "Instrument", "Choir, vocal synth and vocoder from MIDI. Its side-chain input "
                  "(\"Voice In\") feeds a vocal track to the vocoder and auto-tune."],
                 ["<b>Voices FX.vst3</b>", "Effect", "Insert it on a vocal track for auto-tune and the voice changer. "
                  "It also accepts MIDI for the vocoder, MIDI-controlled tuning and the synths."],
                 ["<b>Voices</b> (app)", "Standalone", "Sing into a microphone and hear everything live; play the "
                  "on-screen keys or a MIDI keyboard."]],
                [1.3 * inch, 0.9 * inch, 4.3 * inch]),
          Spacer(1, 6), P("All three share the same engine, editor and patches."), PageBreak()]

# 2
story += [P("2  Installing", "h1"), P("Windows (64-bit)", "h2")]
story += bullets(["Copy the <b>Voices.vst3</b> and <b>Voices FX.vst3</b> folders to "
                  "<font face='DecuMono'>C:\\Program Files\\Common Files\\VST3\\</font> and rescan plug-ins in your DAW.",
                  "The Standalone <b>Voices.exe</b> runs from anywhere. If Windows says it protected your PC, click "
                  "<i>More info &gt; Run anyway</i> (the app isn't signed with a paid certificate)."])
story += [P("Linux (x86_64)", "h2")]
story += bullets(["<font face='DecuMono'>mkdir -p ~/.vst3 &amp;&amp; cp -r Voices.vst3 \"Voices FX.vst3\" ~/.vst3/</font>",
                  "Run <font face='DecuMono'>./Voices</font> for the Standalone (ALSA or JACK)."])

# 3
story += [P("3  Quick start: hear yourself live", "h1"), P("In the Standalone", "h2")]
story += bullets(["Open <b>Options &gt; Audio/MIDI Settings</b>: choose your audio interface (ASIO or Windows Audio "
                  "Exclusive Mode for the lowest latency), your microphone input, and tick your MIDI keyboard.",
                  "<b>Put on headphones</b>, then click <i>Settings...</i> on the yellow \"Audio input is muted\" bar and "
                  "untick <i>Mute audio input</i>.",
                  "Sing: the AUTO-TUNE screen shows your pitch (cyan) and the tuned pitch (pink). Press <b>HARD</b> for "
                  "the stepped robot effect, <b>SUBTLE</b> for gentle correction.",
                  "Play chords on the bottom keyboard for the choir. Turn on VOCODER and sing while holding a chord."])
story += [P("In a DAW", "h2"),
          P("Insert <b>Voices FX</b> on your vocal track and switch on the track's input monitoring (or record-arm "
            "with monitoring). You hear the processed voice while you sing, before you record. The voice path has "
            "about 12 ms of latency, which Voices FX reports to the DAW so recorded takes line up. For the lowest "
            "monitoring delay, set your audio interface to a small buffer (64-256 samples)."),
          PageBreak()]

# 4
story += [P("4  Routing in your DAW", "h1"),
          table([["YOU WANT", "DO THIS"],
                 ["Auto-tune / voice changer", "Voices FX as an insert on the vocal track."],
                 ["Choir or vocal synth", "Voices (instrument) on a MIDI / instrument track."],
                 ["Vocoder with chords", "Voices on a MIDI track with its side-chain input set to the vocal track - or "
                  "Voices FX on the vocal track with a MIDI track routed to it (in DAWs that send MIDI to effects, "
                  "such as Reaper, Cubase and Bitwig)."],
                 ["Vocoder without a keyboard", "Voices FX on the vocal track: VOCODER on, AUTO on, VOICE turned down."],
                 ["Tune to the chords you play", "AUTO-TUNE &gt; NOTES FROM: MIDI, and send MIDI to the plug-in."]],
                [1.9 * inch, 4.6 * inch]),
          Spacer(1, 8),
          note("<b>Ableton Live:</b> VST3 effects can't receive MIDI there, so for the vocoder with chords use the "
               "<b>Voices</b> instrument and pick the vocal track in its side-chain (Sidechain section of the device).",
               CYAN)]

def steps(items): return [Paragraph(t, S["bul"], bulletText=f"{n}.") for n, t in enumerate(items, 1)]

story += [P("Step by step: live vocoder with the Voices instrument", "h2"),
          P("Play chords on a MIDI keyboard and sing into your mic, with the mic feeding Voices' side-chain. "
            "Menu names differ a little between DAWs."),
          *steps(["Add an <b>instrument track</b> with Voices on it, and an <b>audio track</b> for your microphone "
                  "(the mic track).",
                  "Make the mic track Voices' <b>side-chain input</b>. You set this in your DAW, not inside Voices: look "
                  "for the side-chain selector in the plug-in window's header or in the instrument track's routing. "
                  "When it's connected, <b>VOICE IN</b> in Voices' header moves as you sing (SIDE-CHAIN OFF means it "
                  "isn't connected yet).",
                  "Turn <b>monitoring on</b> (or record-arm with monitoring) on <b>both</b> tracks, so you hear the "
                  "vocoder live. Use headphones to avoid feedback.",
                  "Switch <b>VOCODER</b> on, hold a chord and sing."]),
          P("Shaping what you hear", "h3"),
          *bullets(["<b>No dry voice:</b> set the mic track's output to <b>No Bus</b> (None / no output). The side-chain "
                    "still feeds Voices, but the unprocessed voice no longer reaches your speakers, live or on playback.",
                    "<b>Vocoder only:</b> turn <b>VOICE</b> (in the AUTO-TUNE panel) all the way down. This removes "
                    "Voices' own copy of your voice and leaves the vocoder, plus any synth engines that are on."]),
          P("Playing back a recording", "h3"),
          *bullets(["Record your vocal on the mic track and your chords as MIDI on the Voices track.",
                    "For playback, turn the mic track's <b>monitoring off</b>. While it's on, most DAWs pass the live "
                    "mic instead of the recorded take, so Voices would vocode the mic, not your recording.",
                    "Voices processes the take again on every playback, so you can keep changing the sound. Bounce or "
                    "freeze the Voices track when you want the result as audio."]),
          PageBreak()]

# 5
story += [P("5  The header", "h1"),
          shot("main_view.png", caption="The Voices editor (Standalone). Top row: the voice-input engines. "
                                        "Bottom row: the MIDI-played synths, arpeggiator and FX."),
          table([["CONTROL", "WHAT IT DOES"],
                 ["<b>MALE / FEMALE / MIXED</b>", "Switches the whole instrument: the choir's sections, the vocal synth's "
                  "voice (MIXED = duet), the vocoder's colour and the voice changer's direction."],
                 ["<b>Patch LCD</b>", "Shows the patch (a * means edited). Click it for the patch menu, arrows to step. "
                  "While you turn a knob it shows that knob's value."],
                 ["<b>VOICE IN</b>", "Input level of the voice. Voices FX also shows its latency. \"SIDE-CHAIN OFF\" "
                  "means the instrument's side-chain isn't connected."],
                 ["<b>MIDI LEARN</b>", "Assigns the knobs and faders of a MIDI controller to Voices' controls "
                  "(see below). Right-click it to clear every assignment."]],
                [1.7 * inch, 4.8 * inch]),
          Spacer(1, 6),
          P("Double-click any knob to reset it (or right-click &gt; Reset). Drag the bottom-right corner to resize. "
            "The keyboard along the bottom plays C1 to C6.")]
story += [PageBreak()]
story += [P("MIDI learn", "h2"),
          P("Play Voices from a MIDI controller's knobs and faders - it works the same as in Decu6000."),
          *bullets(["Click <b>MIDI LEARN</b> in the header. Every knob, switch and button row gets a yellow outline; "
                   "assigned ones show their CC number.",
                   "Click a control - it says MOVE CC - then move a knob or fader on your controller. The LCD confirms, "
                   "e.g. CHOIR SIZE &lt;- CC 21. Click the next control, and so on.",
                   "Right-click a control to clear it. Click it again, click empty space or press <b>Esc</b> to cancel; "
                   "press Esc again or click MIDI LEARN to leave learn mode.",
                   "Without learn mode: right-click any knob &gt; <b>Learn MIDI CC...</b> (or <b>Clear MIDI CC</b>).",
                   "Each control listens to one CC; learning it again moves it to the new CC."]),
          shot("learn_view.png", caption="MIDI LEARN mode: RETUNE is on CC 74, the choir's ON switch on CC 80, "
                                         "and the vocal synth's vowel is waiting for a CC."),
          note("Assignments are saved with your DAW project or the Standalone's state, not with patches, so they "
               "stay put when you change sounds. CC 64 is always the sustain pedal and CC 120-127 are never mapped.",
               CYAN)]
story += [PageBreak()]

# 6
story += [P("6  Auto-tune", "h1"),
          P("Voices listens to your voice, works out the nearest note in your key and bends your pitch onto it. "
            "Formants are preserved, so the voice keeps its natural character - unless you want it robotic.")]
story += [controls("tune", {
    "on": "Switches pitch correction on or off.",
    "key": "Root note of your song's key.",
    "scale": "Which notes are allowed. CHROMATIC = every semitone (safest if you don't know the key).",
    "source": "SCALE: tune to the key and scale. MIDI: tune to the notes you hold on a keyboard (falls back to the "
              "scale when no keys are held).",
    "retune": "How fast notes snap. 0 ms = instant, the hard stepped hip-hop effect; ~40 ms = modern pop; 150 ms+ = "
              "transparent.",
    "amount": "How much of the way to the note. Below 100% leaves some of your natural pitch.",
    "humanize": "Lets long notes relax so vibrato survives, while note changes still snap.",
}, accent=ORANGE)]
story += [Spacer(1, 6), controls("voice", {"level": "Level of your processed voice in the mix (turn it down to hear "
                                                     "only the vocoder)."}, accent=ORANGE)]
story += [P("Style buttons", "h2"),
          table([["BUTTON", "RETUNE", "HUMANIZE", "AMOUNT", "SOUND"],
                 ["<b>SUBTLE</b>", "150 ms", "70%", "80%", "Gentle, transparent polish"],
                 ["<b>MODERN</b>", "40 ms", "30%", "100%", "Tight modern pop"],
                 ["<b>HARD</b>", "0 ms", "0%", "100%", "Stepped, robotic hip-hop / trap"]],
                [1.0 * inch, 0.9 * inch, 0.9 * inch, 0.9 * inch, 2.8 * inch]),
          P("The pitch screen", "h2"),
          P("Cyan is what you sing, pink is what comes out. Brighter grid lines are notes in your scale; the target "
            "note is labelled top-right with how many cents you were off (cyan within 10 cents)."),
          note("<b>For the hard effect:</b> set the right KEY and SCALE (a wrong key makes notes jump to odd places), "
               "press HARD, and sing with clear note changes. Slides between notes give the classic \"warble\".", PINK)]
story += [PageBreak()]

# 7
story += [P("7  Voice changer", "h1"),
          P("Pick <b>MALE</b>, <b>FEMALE</b> or <b>MIXED</b> in the header and turn <b>AMOUNT</b>. Pitch and formants "
            "move together, so it sounds like a different person rather than a sped-up tape. At 100% the pitch moves "
            "6 semitones and the formants 3 semitones."),
          controls("chg", {
              "on": "Switches the voice changer on or off.",
              "amount": "How strong the change is, toward the voice type picked in the header.",
              "pitch": "Extra pitch shift on top (keeps formants).",
              "formant": "Extra formant shift: down = bigger, deeper throat; up = smaller, younger.",
              "width": "MIXED only: how far apart the male and female layers sit in stereo.",
          }, accent=ORANGE),
          Spacer(1, 6),
          note("Auto-tune and the voice changer work together in one pass - the changed voice is tuned too.", ORANGE)]

# 8
story += [P("8  Vocoder", "h1"),
          P("Your voice (the modulator) is split into bands; each band's loudness shapes the same band of a built-in "
            "synth (the carrier). Play chords and sing - the synth talks."),
          controls("voc", {
              "on": "Switches the vocoder on or off.",
              "bands": "Number of bands. Fewer = classic 80s robot; more = clearer words.",
              "carrier": "SAW: bright and buzzy. PULSE: hollow. SUPER: a lush detuned stack.",
              "release": "How quickly bands fall silent - short = choppy and precise, long = smooth.",
              "formant": "Shifts the carrier bands: down = bigger, darker; up = smaller, brighter.",
              "sibilance": "Lets the voice's \"s\", \"t\", \"sh\" through for intelligibility.",
              "auto": "With no keys held, the carrier sings the pitch you sing (after auto-tune) - no keyboard needed.",
              "level": "Vocoder volume.",
          }, accent=ORANGE),
          Spacer(1, 6),
          P("MALE and MIXED voice types add a sub-octave to the carrier; FEMALE shifts the bands up a little.")]
story += [PageBreak()]

# 9
story += [P("9  Choir", "h1"),
          P("The classic keyboard choir, synthesised. Every note you play is sung by eight singers, each slightly "
            "detuned and drifting, with their own vibrato and place in the stereo field, through vowel formants. "
            "MIXED splits every note between a male and a female section."),
          controls("choir", {
              "on": "Switches the choir on or off.",
              "vowel": "Morph from OOH through OH to AAH (or use the buttons).",
              "size": "Ensemble spread: detune, drift and stereo width.",
              "breath": "Breath noise sung through the vowel.",
              "vibrato": "Vibrato depth (each singer at a slightly different speed).",
              "attack": "Fade-in time - long for pads, short for stabs.",
              "release": "Fade-out time after you let go.",
              "level": "Choir volume.",
          }, accent=CYAN)]

# 10
story += [P("10  Vocal synth", "h1"),
          P("A solo singing voice. Pick a vowel with the <b>A E I O U</b> buttons or morph between them with the VOWEL "
            "knob - automate it for \"talking\" lines. Vibrato fades in naturally after the note starts. MIXED makes "
            "it a duet with a male voice an octave below."),
          controls("vsyn", {
              "on": "Switches the vocal synth on or off.",
              "vowel": "A, E, I, O, U and everything in between.",
              "vibrato": "Vibrato depth.",
              "breath": "Breathiness.",
              "bright": "Darker / softer vs. brighter / buzzier voice.",
              "glide": "Slide time from the previous note.",
              "attack": "Fade-in time.",
              "release": "Fade-out time.",
              "level": "Vocal synth volume.",
          }, accent=CYAN)]
story += [PageBreak()]

# 11
story += [P("11  Arpeggiator", "h1"),
          P("Hold a chord and the arpeggiator plays it for the choir, vocal synth and vocoder. It follows your DAW's "
            "tempo: while the DAW plays it locks to the bar grid; when stopped it starts the moment you press a key."),
          controls("arp", {
              "on": "Switches the arpeggiator on or off.",
              "mode": "TREM: the whole chord repeats. UP / DOWN: one note at a time. RANDOM: never the same note twice "
                      "in a row.",
              "octaves": "How many octaves the pattern climbs (1-4).",
              "rate": "Step length, including triplets (T).",
              "gate": "How long each step sounds, as a share of the step.",
              "latch": "Keeps playing after you let go; play a new chord to replace it.",
          }, accent=CYAN),
          shot("arp_view.png", width=6.0 * inch, caption="ANGEL ARP: choir arpeggio over three octaves with the "
                                                         "voice changer on.")]

# 12
story += [P("12  FX and master", "h1"),
          P("Everything - your voice and the synths - goes through chorus, then delay, then reverb. Each has its own "
            "MIX; at 0 it is off.")]
story += [controls("chorus", {"rate": "Chorus speed.", "depth": "How much the voices sway.", "mix": "Chorus amount."}, accent=PINK),
          Spacer(1, 4),
          controls("delay", {"time": "Echo time, synced to tempo (D = dotted, T = triplet).",
                             "feedback": "How many repeats. Repeats bounce left / right and get darker.",
                             "mix": "Echo level."}, accent=PINK),
          Spacer(1, 4),
          controls("reverb", {"size": "Room size and length (about 1 to 9 seconds).", "damp": "Darker tail when up.",
                              "mix": "Reverb amount."}, accent=PINK),
          Spacer(1, 4),
          controls("master", {"level": "Output volume (a soft limiter stops overs).",
                              "voicetype": "Male / female / mixed (the header buttons)."}, accent=PINK)]
story += [PageBreak()]

# 13
story += [P("13  Patches", "h1"),
          P("Click the LCD for the patch menu: factory patches, your patches, Save, Save As, Delete and Open patch "
            "folder. A patch stores every sound setting. Your DAW project remembers the patch and any unsaved tweaks."),
          table([["PATCH", "WHAT IT IS"],
                 ["INIT", "Mixed choir aahs, gentle chromatic tuning on any voice input."],
                 ["CATHEDRAL AAHS", "Big, slow mixed choir in a long reverb."],
                 ["VOICE OOHS", "Soft female oohs."],
                 ["SUBTLE POLISH", "Transparent tuning (C major) for lead vocals - set your key."],
                 ["HARD TUNE // TRAP", "Instant stepped tuning in A minor with a dotted-eighth delay."],
                 ["ROBOT CHOIR", "Super-saw vocoder that follows your voice (AUTO) over a quiet choir."],
                 ["DAFT VOCODER", "Classic 16-band saw vocoder, male, play chords."],
                 ["DIVA LEAD", "Female vocal synth on \"A\" with expressive vibrato and glide."],
                 ["ANGEL ARP", "Female choir arpeggio over three octaves, delay and big reverb."],
                 ["TREM VOICES", "Tremolo chords on vocal synth \"O\" and choir."],
                 ["DEEP VOICE", "Voice changer: deeper, male."],
                 ["ANIME GIRL", "Voice changer: higher, female, with light tuning."],
                 ["DUET DOUBLE", "Voice changer MIXED: male and female doubles left and right."]],
                [1.6 * inch, 4.9 * inch]),
          Spacer(1, 8),
          P("User patches are small <font face='DecuMono'>.voicespatch</font> files you can copy and share:"),
          ]
story += bullets(["Windows: <font face='DecuMono'>%APPDATA%\\Voices Synth\\Patches</font>",
                  "macOS: <font face='DecuMono'>~/Library/Voices Synth/Patches</font>",
                  "Linux: <font face='DecuMono'>~/.config/Voices Synth/Patches</font>"])

# 14
story += [P("14  Recipes", "h1")]
recipes = [
    ("T-Pain / trap hook", "HARD style, correct KEY + SCALE (minor keys suit trap), delay 1/8D at 15-20%, a touch of chorus."),
    ("Invisible tuning", "SUBTLE style, AMOUNT 60-80%, HUMANIZE 70%+. Nobody should hear it."),
    ("Yamaha-style choir pad", "Choir AAH, ENSEMBLE 70%, ATTACK 400 ms, RELEASE 1.5 s, chorus 40%, reverb SIZE 90% MIX 45%."),
    ("Choir follows your song", "Voices on a MIDI track; play the chords; turn on ARP TREM 1/16 for a pulsing stab."),
    ("Robot without a keyboard", "Voices FX: VOCODER on, AUTO on, VOICE 0%, HARD tuning, CARRIER SAW, BANDS 12-16."),
    ("Talking vocal synth", "Automate VOWEL from A to U across a phrase; GLIDE 60 ms; delay on."),
    ("Gender-flip a demo vocal", "Voice changer ON, FEMALE or MALE, AMOUNT 60-100%; nudge FORMANT if it sounds too cartoonish."),
    ("Wide double", "Voice changer MIXED, AMOUNT 20-40%, WIDTH 100% - a natural-sounding stereo double."),
]
story += [table([["IDEA", "TRY THIS"]] + [[f"<b>{a}</b>", b] for a, b in recipes], [1.8 * inch, 4.7 * inch])]

# 15
story += [P("15  Troubleshooting", "h1")]
trouble = [
    ("I can't hear my voice (Standalone)", "Untick \"Mute audio input\" (Settings... on the yellow bar), check the "
                                           "input device, and that VOICE is up."),
    ("Feedback howl", "Use headphones when monitoring a microphone."),
    ("VOICE IN says SIDE-CHAIN OFF", "In the instrument, connect the vocal track to Voices' side-chain input in your "
                                     "DAW - or use Voices FX on the vocal track instead."),
    ("Tuning jumps to wrong notes", "KEY / SCALE don't match the song. Use CHROMATIC if unsure, or NOTES FROM: MIDI."),
    ("Tuning sounds warbly on held notes", "Raise RETUNE or HUMANIZE (or use SUBTLE)."),
    ("Vocoder is silent", "It needs both a voice and a carrier: hold keys, or turn AUTO on. Check VOCODER LEVEL."),
    ("Synths don't play", "Make sure MIDI reaches the plug-in and the engine's ON button is lit."),
    ("Voices FX gets no MIDI", "Your DAW may not send MIDI to effects - use the Voices instrument with side-chain."),
    ("A controller knob does nothing", "Assign it with MIDI LEARN first, and make sure your DAW passes CCs to the plug-in. "
                                       "The sustain pedal (CC 64) can't be assigned."),
    ("Windows says the app is unsafe", "Click More info &gt; Run anyway. The app isn't signed with a paid certificate."),
]
story += [table([["PROBLEM", "WHAT TO DO"]] + [[f"<b>{a}</b>", b] for a, b in trouble], [2.1 * inch, 4.4 * inch])]
story += [PageBreak()]

# 16
story += [P("16  Credits and license", "h1"),
          P("Voices is free software: you can redistribute it and/or modify it under the terms of the "
            "<b>GNU Affero General Public License</b>, version 3 or (at your option) any later version. It is "
            "distributed in the hope that it will be useful, but <b>without any warranty</b>; see the LICENSE file."),
          P("Built with", "h2")]
story += bullets([
    "<b>JUCE 8</b> (juce.com), used under the AGPLv3.",
    "<b>VST 3 SDK</b> by Steinberg Media Technologies GmbH. VST is a trademark of Steinberg Media Technologies GmbH; "
    "Voices is not affiliated with Steinberg.",
    "<b>Orbitron</b> by The League of Moveable Type (instanced and renamed \"Decu Display\") and <b>Share Tech Mono</b> "
    "by Carrois Type Design - both under the SIL Open Font License 1.1.",
    "Formant data: classic singing-voice formant tables (tenor and alto) widely used in formant synthesis.",
])
story += [Spacer(1, 10), P(f"Voices - Copyright (C) 2026 LexLongcoding. Manual for version {VERSION}.", "cap")]

# ---------------------------------------------------------------- build
doc = BaseDocTemplate(OUT, pagesize=letter, leftMargin=inch, rightMargin=inch, topMargin=0.85 * inch,
                      bottomMargin=0.8 * inch, title="Voices User Manual", author="LexLongcoding",
                      subject=f"Voices {VERSION} - vocal / choir synthesizer, vocoder, auto-tune",
                      creator="docs/manual/make_manual.py")
frame = Frame(doc.leftMargin, doc.bottomMargin, doc.width, doc.height, id="f")
doc.addPageTemplates([PageTemplate(id="cover", frames=[frame], onPage=on_cover),
                      PageTemplate(id="body", frames=[frame], onPage=on_page)])
doc.build(story)
print("wrote", OUT, "-", len(SPECS), "parameters documented")
