/* SPDX-License-Identifier: GPL-3.0-only */
/* The sound of the five scenes (runs in the audio ISR: integers only, no flash, no screen). Included by tripdeck.c.
 *
 *   scene 1  KALEIDO  ring modulator   MIRROR carrier pitch | DEPTH wet | SPIN carrier wobble | COLOR sine -> square
 *   scene 2  TUNNEL   phaser           TWIST resonance      | RINGS depth | SPEED sweep rate   | COLOR centre
 *   scene 3  PLASMA   chorus / flanger SCALE delay          | WARP depth  | SPEED rate         | COLOR flange feedback
 *   scene 4  MELT     tape echo        ZOOM time            | TRAIL feedback | SWIRL wow       | COLOR darkness
 *   scene 5  WARP     crush + warble   PIXEL bits and rate  | DEPTH wet   | SPEED warble      | COLOR tone
 *
 * The same four numbers (SP) drive the picture in scene_vis.h, so a turn changes both. */

#include "vis_tables.h"                        /* SIN8, ANG, RAD, INVR for the pictures; RING_INC here */

static volatile uint8_t scene_cur;              /* main -> ISR: 0 none, 1..5 */
static volatile uint8_t SP[5][4];               /* the scene knobs, written by the main loop */
static volatile uint32_t vis_pk;                /* ISR -> main: the output peak, for the picture */

static int32_t sine(uint32_t ph)                /* a full cycle over 2^32 -> -32767..32767 (a refined parabola, ~0.1 %) */
{
    int32_t x = (int32_t)ph >> 16, ax = x < 0 ? -x : x, y, ay;
    y = (x * (32768 - ax)) >> 13;
    ay = y < 0 ? -y : y;
    y += ((((y * ay) >> 15) - y) * 7373) >> 15;
    return y > 32767 ? 32767 : y < -32767 ? -32767 : y;
}

static int16_t chL[2048] __attribute__((section(".pool")));      /* the chorus lines: 46 ms */
static int16_t chR[2048] __attribute__((section(".pool")));

static uint32_t fxs, fxg;                       /* the scene whose effect runs, and its wet gain 0..128 (a ramp when it changes) */
static uint32_t sph, rph, cw, hcnt;             /* LFO phase, ring carrier phase, chorus write position, crusher counter */
static int32_t slfo, slfo2;                     /* the scene LFO now, and 90 degrees later (the right channel) */
static int32_t apx[2][4], apy[2][4], apo[2];    /* the phaser: four all-pass stages per channel */
static int32_t hL, hR, oL, oR;                  /* the crusher: held samples, then the tone filter */
static int32_t r_inc, r_sq, s_wet;              /* ring: carrier step, square blend; the wet amount (all scenes) */
static int32_t p_fb, p_aL, p_aR;                /* phaser: feedback, all-pass coefficients */
static int32_t c_d0, c_mL, c_mR, c_fb;          /* chorus: base delay and modulation (Q8 samples), feedback */
static int32_t x_mask, x_hold, x_a;             /* crush: bit mask, hold length, tone filter */
/* the delay of the patch is borrowed by scene 4: the values the delay uses now */
static int32_t e_dmix, e_dfb, e_dt, e_lpsh = 2, e_wow;

static void scene_tick(void)                    /* every 16 frames */
{
    uint32_t sc = scene_cur, k1, k2, k3, k4, ri;
    int32_t g;

    if (fxs != sc) { fxg = fxg > 4u ? fxg - 4u : 0u; if (!fxg) fxs = sc; }      /* fade the old effect out, then the new one in */
    else if (fxs && fxg < 128u) fxg += 4u;
    g = (int32_t)fxg;

    e_dmix = P[P_DMIX];                         /* the patch's own delay, unless scene 4 takes it */
    e_dfb = (int32_t)((P[P_DFB] * 113u) >> 7);
    e_dt = 1500 + (int32_t)P[P_DTIME] * 245;
    e_lpsh = 2; e_wow = 0;
    if (!fxs) return;

    k1 = SP[fxs - 1u][0]; k2 = SP[fxs - 1u][1]; k3 = SP[fxs - 1u][2]; k4 = SP[fxs - 1u][3];
    ri = fxs == 1u ? 20u + (k3 * 80u >> 7) : fxs == 2u ? 20u + (k3 * 80u >> 7) : fxs == 3u ? 15u + (k3 * 70u >> 7)
       : fxs == 4u ? 30u + (k3 * 50u >> 7) : 40u + (k3 * 50u >> 7);
    sph += LFO_INC[ri];
    slfo = sine(sph);
    slfo2 = sine(sph + 0x40000000u);
    s_wet = (int32_t)k2;

    switch (fxs) {
    case 1: {                                   /* ring modulator */
        int32_t wob = (int32_t)(((int64_t)RING_INC[k1] * slfo) >> 15);
        r_inc = (int32_t)RING_INC[k1] + ((wob * (int32_t)k3) >> 10);
        r_sq = (int32_t)k4;
        break; }
    case 2: {                                   /* phaser */
        int32_t ac = 4900 + (int32_t)k4 * 142, da = (int32_t)(((int64_t)slfo * (int32_t)k2 * 11500) >> 22),
                db = (int32_t)(((int64_t)slfo2 * (int32_t)k2 * 11500) >> 22);
        p_aL = ac + da; p_aR = ac + db;
        p_aL = p_aL < 1500 ? 1500 : p_aL > 30000 ? 30000 : p_aL;
        p_aR = p_aR < 1500 ? 1500 : p_aR > 30000 ? 30000 : p_aR;
        p_fb = (int32_t)(k1 * 100u >> 7);
        break; }
    case 3:                                     /* chorus and flanger */
        c_d0 = (int32_t)(40u + k1 * 8u) << 8;
        c_mL = (slfo * (int32_t)k2) >> 6;
        c_mR = (slfo2 * (int32_t)k2) >> 6;
        c_fb = (int32_t)(k4 * 90u >> 7);
        break;
    case 4:                                     /* tape echo: the patch's delay, taken over */
        e_dmix = (e_dmix * (128 - g) + 96 * g) >> 7;
        e_dfb = (e_dfb * (128 - g) + (int32_t)((k2 * 113u) >> 7) * g) >> 7;
        e_dt = (e_dt * (128 - g) + (1500 + (int32_t)k1 * 245) * g) >> 7;
        e_lpsh = 1 + (int32_t)(k4 * 5u >> 7);
        e_wow = (((slfo * (int32_t)k3) >> 17) * g) >> 7;
        break;
    default: {                                  /* crush and rate warble */
        uint32_t bits = 16u - (k1 * 11u >> 7), hold = 1u + (k1 * 14u >> 7);
        hold += (uint32_t)(((slfo + 32768) * (int32_t)(k3 >> 3)) >> 16);
        x_mask = ~((1 << (16u - bits)) - 1);
        x_hold = (int32_t)hold;
        x_a = 6000 + (127 - (int32_t)k4) * 200;
        break; }
    }
}

static inline int32_t cl15(int32_t v) { return v > 32767 ? 32767 : v < -32767 ? -32767 : v; }

static inline void scene_frame(int32_t *pL, int32_t *pR)   /* every frame, after the patch's own effects */
{
    int32_t L = *pL, R = *pR, fL = L, fR = R, w = s_wet, g = (int32_t)fxg, ch;
    if (!fxs || !fxg) return;
    switch (fxs) {
    case 1: {                                   /* ring modulator */
        int32_t c, sq, Lc = L > 65535 ? 65535 : L < -65535 ? -65535 : L, Rc = R > 65535 ? 65535 : R < -65535 ? -65535 : R;
        rph += (uint32_t)r_inc;
        sq = (rph >> 31) ? -32767 : 32767;
        c = (sine(rph) * (127 - r_sq) + sq * r_sq) >> 7;
        fL = (L * (127 - w) + (((Lc >> 1) * c) >> 14) * w) >> 7;
        fR = (R * (127 - w) + (((Rc >> 1) * c) >> 14) * w) >> 7;
        break; }
    case 2: {                                   /* phaser: four all-pass stages, swept; the dry plus the shifted */
        for (ch = 0; ch < 2; ch++) {
            int32_t x = cl15(ch ? R : L), a = ch ? p_aR : p_aL, in = x + ((apo[ch] * p_fb) >> 7), s;
            for (s = 0; s < 4; s++) {
                int32_t y = (int32_t)(((int64_t)a * (in - apy[ch][s])) >> 15) + apx[ch][s];
                y = y > 400000 ? 400000 : y < -400000 ? -400000 : y;
                apx[ch][s] = in; apy[ch][s] = y; in = y;
            }
            apo[ch] = cl15(in);
            if (ch) fR = (x + in) >> 1; else fL = (x + in) >> 1;
        }
        break; }
    case 3: {                                   /* chorus / flanger: a delay line read between samples, swung by the LFO */
        int32_t dl = c_d0 + c_mL, dr = c_d0 + c_mR, pl, pr, yl, yr, il, ir, fl, fr;
        dl = dl < 256 ? 256 : dl > (2040 << 8) ? (2040 << 8) : dl;
        dr = dr < 256 ? 256 : dr > (2040 << 8) ? (2040 << 8) : dr;
        pl = (int32_t)(cw << 8) - dl; pr = (int32_t)(cw << 8) - dr;
        il = (pl >> 8) & 2047; ir = (pr >> 8) & 2047; fl = pl & 255; fr = pr & 255;
        yl = (chL[il] * (256 - fl) + chL[(il + 1) & 2047] * fl) >> 8;
        yr = (chR[ir] * (256 - fr) + chR[(ir + 1) & 2047] * fr) >> 8;
        chL[cw] = (int16_t)cl15(L + ((yl * c_fb) >> 7));
        chR[cw] = (int16_t)cl15(R + ((yr * c_fb) >> 7));
        cw = (cw + 1u) & 2047u;
        fL = ((L + yl) * 90) >> 7;
        fR = ((R + yr) * 90) >> 7;
        break; }
    case 4:                                     /* the echo lives in the patch's delay (scene_tick sets its values) */
        return;
    default: {                                  /* crush and warble */
        int32_t Lc = L > 65535 ? 65535 : L < -65535 ? -65535 : L, Rc = R > 65535 ? 65535 : R < -65535 ? -65535 : R;
        if (++hcnt >= (uint32_t)x_hold) { hcnt = 0; hL = Lc & x_mask; hR = Rc & x_mask; }
        oL += (((hL - oL) >> 2) * x_a) >> 13;
        oR += (((hR - oR) >> 2) * x_a) >> 13;
        fL = (L * (127 - w) + oL * w) >> 7;
        fR = (R * (127 - w) + oR * w) >> 7;
        break; }
    }
    *pL = (L * (128 - g) + fL * g) >> 7;        /* crossfade with the dry sound while the effect fades in or out */
    *pR = (R * (128 - g) + fR * g) >> 7;
}
