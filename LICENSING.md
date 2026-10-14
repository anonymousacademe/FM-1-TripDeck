# Licensing

TripDeck is free software under the GNU General Public License, version 3 only (`GPL-3.0-only`, full text in
`LICENSE`). You may use, study, change and share it. If you give someone a firmware built from it, give them its
complete source under the same licence too.

## Who made what

| | |
| --- | --- |
| The platform: `firmware/hal/`, `firmware/loader/`, `crt0.S`, `app.ld`, `firmware/src/` (`usb.c`, `ota.c`, `recovery.c`, `lcd.c`, `gfx.c`, `libc.c`, `bootguard.h`, `uac_fir.h`), `tools/` | Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments ([Felucca](https://github.com/hugelton/Felucca)), with the changes of [SLOOP](https://github.com/isod89/sloop-fm1) (isod89) |
| The wiring `platform.c`, the host mode `tests/host.c` and the docs it comes with | zvenson ([Dinghy](https://github.com/zvenson/dinghy)) |
| CyberDeck: the synth voice, arpeggiator, sequencer and patch mutator that TripDeck builds on | the CyberDeck project of this family of firmwares |
| TripDeck: `firmware/src/tripdeck.c`, `voices.h`, `fx.h`, `scene_vis.h`, `scene_audio.h`, the tests and `docs/MANUAL.md` | anonymousacademe, GPL-3.0-only |

## Third-party material

| What | Licence | Where |
| --- | --- | --- |
| Terminus font 8x16 (ter-u16n), rendered into `firmware/gen/felucca_font.h` | SIL OFL 1.1 | `assets/fonts/Terminus-LICENSE.txt` |
| JieLi AC79 SDK: `uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin` are read from your SDK checkout at build time and placed in the package; no SDK files are in this tree | Apache-2.0 | <https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK> |

## The logo

The designburg signet that Dinghy carried is not part of TripDeck (it is Copyright (C) designburg, all rights reserved, and marks Dinghy itself).

## Trademarks

"Felucca" and "Hügelton Instruments" are names of Hügelton Instruments; "M-VAVE" and "FM-1" belong to their
owners. TripDeck is not made or endorsed by them.
