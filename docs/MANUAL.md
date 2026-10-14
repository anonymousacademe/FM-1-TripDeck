# TRIPDECK

A four-voice subtractive synth with five **psychedelic scenes**. Each scene puts a full-screen animated picture on the display
and an effect on the sound, and the same four knobs change both at once. The pictures also move with what you play.
Package identity `FM-1_908`. GPL-3.0-only. Built on CyberDeck and Dinghy; platform by Felucca and SLOOP.

## The scenes

Press a scene button to enter it. Press the same button again (or SEL) to leave.

| Button | Scene | Picture | Sound effect |
|---|---|---|---|
| **EDIT** | KALEIDO | A mirrored mandala | **Ring modulator**: bell and metal tones |
| **ENV** | TUNNEL | A spiraling tunnel you fly down | **Phaser**: a swirling sweep |
| **LFO** | PLASMA | Flowing liquid color | **Chorus / flanger**: shimmer and width |
| **FX** | MELT | Video feedback: sparks that smear into trails | **Tape echo** with wow and flutter |
| **GLO** | WARP | A hyperspace starfield | **Bit crush** and a rate warble |

### The four knobs (KNOB 1-4)

They always mean the same kind of thing: **shape, depth, motion, color**. The labels on the bottom of the screen name them in each scene.

| Scene | Knob 1 | Knob 2 | Knob 3 | Knob 4 |
|---|---|---|---|---|
| KALEIDO | MIRROR: copies in the picture / ring pitch | DEPTH: rings / amount of effect | SPIN: rotation / carrier wobble | COLOR: hue / sine to square carrier |
| TUNNEL | TWIST: spiral / resonance | RINGS: ring density / sweep depth | SPEED: flight speed / sweep rate | COLOR: hue / sweep center |
| PLASMA | SCALE: size of the blobs / chorus delay | WARP: liquid distortion / chorus depth | SPEED: drift / LFO rate | COLOR: hue / flange feedback |
| MELT | ZOOM: in or out / echo time | TRAIL: how long trails last / feedback | SWIRL: rotation / wow | COLOR: hue / echo darkness |
| WARP | PIXEL: chunky pixels / bit and rate crush | DEPTH: star trails / amount of effect | SPEED: star speed / rate warble | COLOR: hue / tone filter |

When you turn a knob, the top line shows its name and value in yellow for a moment, and its bar lights up.

**HOME** (tap) in a scene gives you a *surprise*: four new knob positions.

The pictures react to the sound: the louder you play, the more they zoom, speed up and spark, and a held key puts a bright star or spark on screen.

## Everything else (as in CyberDeck)

- **SEL** leaves the scene and shows the synth pages. Each further press steps through OSC, FLT, ENV, LFO, FX, GLO (the knobs then edit the patch).
- **ARP**, **SEQ**, **PLAY**, **REC**, **OCT-/+**, **SAVE**, **ALGORITHM** (cutoff), **SELECT** (resonance), **PRESETS** (8 patches) all work.
  In a scene, ARP cycles on, hold, off without leaving. SEQ shows the step grid (it leaves the scene).
- The top line of a scene shows the tempo, `ARP`/`HLD`, `>` (playing) and `REC`.
- **SAVE** also stores your scene knob positions.
- HOME held: panic. HOME tapped outside a scene: mutate the patch.

See `..\cyberdeck\docs\MANUAL.md` for the synth, arpeggiator and sequencer in detail.

## Notes

- A frame is sent to the screen in eight strips, so you may see a faint seam move down the picture. That is normal.
- The screen link runs at 12 MHz (about 12 frames per second). It can be raised later (`LCD_BAUD`) if the display tolerates it.
