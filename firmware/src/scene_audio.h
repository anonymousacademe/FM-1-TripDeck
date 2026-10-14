/* SPDX-License-Identifier: GPL-3.0-only */
/* What the sound and the pictures share (audio ISR side: integers only, no flash, no screen). Included by tripdeck.c.
 *
 * `kick` is set on every new note and fades in about 0.2 s. The pictures (scene_vis.h) and the voices both follow it, so a note
 * strikes the sound and the picture at once. The ISR also tells the main loop the output level and the patch LFO. */

#include "vis_tables.h"                        /* SIN8, ANG, RAD, INVR for the pictures */

static volatile uint32_t vis_pk;                /* ISR -> main: the output peak, for the picture */
static volatile uint32_t vis_kick;              /* ISR -> main: 0..255, a note has just started */
static volatile int32_t vis_lfo;                /* ISR -> main: the patch LFO now, +-32512 */
static int32_t kick;                            /* Q15; app_audio sets it on every new note */

static int32_t sine(uint32_t ph)                /* a full cycle over 2^32 -> -32767..32767 (a refined parabola, ~0.1 %) */
{
    int32_t x = (int32_t)ph >> 16, ax = x < 0 ? -x : x, y, ay;
    y = (x * (32768 - ax)) >> 13;
    ay = y < 0 ? -y : y;
    y += ((((y * ay) >> 15) - y) * 7373) >> 15;
    return y > 32767 ? 32767 : y < -32767 ? -32767 : y;
}

static inline int32_t cl15(int32_t v) { return v > 32767 ? 32767 : v < -32767 ? -32767 : v; }

static void kick_tick(void)                     /* every 16 frames */
{
    kick -= (kick >> 9) + (kick ? 1 : 0);
    vis_kick = (uint32_t)kick >> 7;
}
