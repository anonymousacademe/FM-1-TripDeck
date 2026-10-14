# TRIPDECK

A four-voice **psychedelic synth** for the FM-1. Every screen is a full-screen animated picture, and the four knobs of the screen change
the sound **and** the picture together. **HOME** hides the picture and shows the plain parameters (and back).
Package identity `FM-1_908`. GPL-3.0-only. Built on CyberDeck and Dinghy; platform by Felucca and SLOOP.

## The screens

There are ten pages of four knobs. Each is a picture; the knobs are the page's four parameters.

| Page | Button | Picture | Knobs 1-4 |
|---|---|---|---|
| **OSC** | EDIT | KALEIDO: a mirrored mandala | VOICE, then three knobs that change with the voice (below) |
| **FLT** | EDIT again | TUNNEL: a spiralling tunnel | CUTOFF, RESO, ENV AMT, DRIVE |
| **ENV** | ENV | WAVES: ribbons of colour | ATTACK, DECAY, SUSTAIN, RELEASE |
| **LFO** | LFO | PLASMA: flowing liquid colour | RATE, DEPTH, DEST, SHAPE |
| **DLY** | FX | MELT: video feedback, sparks that smear into trails | DLY MIX, DLY TIME, FEEDBACK, **SPIRAL** |
| **REV** | FX again | ZONE: rippling rings from two moving centres | REVERB, **SHIMMER**, DAMPING, **FREEZE** |
| **VOX** | FX again | FLOWER: a rotating rose window | **VOWEL**, **TALK**, SHIFT, SWEEP |
| **GLO** | GLO | SPIRAL: a galaxy | GLIDE, **TRIP**, **SCALE**, ROOT |
| **ARP** | ARP | WARP: a hyperspace starfield | ARP MODE, OCTAVES, RATE, GATE |
| **SEQ** | SEQ | HORIZON: a chequered floor and a striped sun | BPM, SWING, LENGTH, TRANSPOSE |

**SEL** steps to the next page. **ALGORITHM** and **SELECT** always change the cutoff and the resonance, **PRESETS** the patch.
In a picture, the top line shows the knob you just turned and its value, and the bottom shows the four knobs as rainbow bars.

### HOME

| | |
|---|---|
| **tap** | the picture <-> the parameters (the four knobs of the page as big meters, the scope or the step grid, the tabs) |
| **double-tap** | mutate the patch: a random walk of a few knobs (never the voice) |
| **hold** | panic: every note off |

(While step recording, a tap enters a rest.)

## The voices

**VOICE** is the first knob of the OSC page and picks the engine; the other three knobs of the page are renamed to match.

| VOICE | Sound | Knob 2 | Knob 3 | Knob 4 |
|---|---|---|---|---|
| **PAD** | Two detuned oscillators through a filter, with a slow tape wobble in the pitch | PULSE W | DETUNE | SUB |
| **SITAR** | A plucked string with a buzzing bridge. CUTOFF is how bright the string is, DECAY how long it rings | BUZZ: bridge buzz | MEEND: a scoop up to the note | DRONE: level of the tamboura |
| **ORGAN** | Drawbar organ through a rotating speaker | DRAWBAR: more and more harmonics | LESLIE: rotor speed (0 = off; it glides up and down like the real thing) | 16 FOOT |
| **BELL** | Two-operator FM: glass bells and a soft electric piano. The tone starts bright and mellows (DECAY) | RATIO: the modulator (8 = 1:1, 24 = 2:1, 48 = 3.5:1, a bell) | INDEX: brightness | TINE: a quick high strike, the hammer of an electric piano |

The **tamboura** hums Sa, Pa, Sa under your playing, tuned to the first note you play. It fades in with the playing and out 8 seconds after you stop.

## The effects

- **Spiral echo** (DLY): a ping-pong echo whose every repeat gets darker and shifted in pitch again. SPIRAL at the middle is no shift; up and the echoes climb, down and they sink.
- **Reverb** (REV): a big stereo reverb with a tail of about 3 seconds. **SHIMMER** feeds an octave-up copy back into the tail so it rises like a choir. **FREEZE** (turn it past the middle) stops the input and holds the tail for ever: play over it. DAMPING darkens the tail.
- **Vowel filter** (VOX): three formant filters. **VOWEL** morphs A - E - I - O - U, **TALK** is how much you hear, SHIFT moves the formants up and down, **SWEEP** makes the vowel move by itself (a talking wah).
- **Scale lock** (GLO): **SCALE** snaps every note (keys, MIDI, arpeggiator, sequencer) to a scale: off, major, minor, dorian, phrygian, bhairav (a raga), minor pentatonic, major pentatonic, whole tone. **ROOT** is the key.
- **TRIP** (GLO): the knobs that colour the sound (the voice knobs, cutoff, resonance, LFO depth, echo feedback, shimmer, damping, vowel and shift) wander on their own, as far as TRIP allows. The bars on the parameter pages move with them.

## What you hear is what you see

Besides the page's four knobs, the picture follows the rest of the sound:

| You change | The picture does | The sound does |
|---|---|---|
| **A new note** (key, arp, sequencer) | Flashes, and the picture kicks | The voice strikes at the same moment |
| **ALGORITHM** (cutoff) | Brightness: dark and closed to bright and open | The filter |
| **SELECT** (resonance) | Contrast: soft to hard colour | The filter resonance |
| **REVERB** | A soft, milky glow over the colours | The reverb |
| **Patch LFO** | The hue swings at the LFO's own rate | The LFO |
| **The note you hold** | Pulls the colours round the wheel to its own hue | The pitch |

## The patches

SKY PAD (pad, shimmer, up-spiral echo), CHOIR (pad through the vowel filter), SUNSHINE (sitar), RAGA DRONE (sitar, heavy buzz), PURPLE ORGAN, GLASS BELLS (bell, shimmer, climbing echoes),
DREAM KEYS (electric piano), SPIRAL DUB (a dub pad with sinking echoes). **SAVE** stores the current patch in its slot, the global settings (GLO, ARP, SEQ pages) and the pattern.

## Everything else (as in CyberDeck)

- **ARP** goes to the ARP page and turns the arpeggiator on; pressed again it holds, then switches off. **SEQ**, **PLAY**, **REC**, **OCT-/+** work as in CyberDeck.
- The top line shows the tempo, `ARP`/`HLD`, `>` (playing) and `REC`.
- See `..\cyberdeck\docs\MANUAL.md` for the arpeggiator and sequencer in detail.

## Notes

- A frame is sent to the screen in eight strips, so you may see a faint seam move down the picture. That is normal.
- The screen link runs at 12 MHz (about 12 frames per second). It can be raised later (`LCD_BAUD`) if the display tolerates it.
- The first time TripDeck starts it replaces the saved settings of any earlier TripDeck or CyberDeck: the layout is different.
- The parameter pages show the CPU load of the audio. If a busy patch makes it climb towards 100 %, turn off the shimmer, the vowel filter or the spiral: they are the costly effects.
