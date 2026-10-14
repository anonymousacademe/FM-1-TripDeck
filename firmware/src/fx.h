/* SPDX-License-Identifier: GPL-3.0-only */
/* The trippy effects (audio ISR: integers only). Included by tripdeck.c after voices.h.
 *
 *   pitch shifter   two crossfaded read heads on a delay line that moves at a different speed than it is written: the pitch shifts.
 *                   Used twice: in the echo (SPIRAL: every repeat is shifted again, so the echoes spiral up or down) and in the
 *                   reverb (SHIMMER: an octave up, fed back into the tail, so it climbs like a choir).
 *   vowel filter    three band-pass filters on the formants of A E I O U; VOWEL morphs between them, SHIFT moves them, SWEEP makes
 *                   them move on their own (a talking wah). TALK is how much of it you hear. */

/* ---- pitch shifter */
#define PSN 2048u                               /* the line */
#define PSW (1024 << 16)                        /* the window: 1024 samples, in 16.16 */
static int16_t psb[3][PSN] __attribute__((section(".pool")));
typedef struct { int16_t *buf; uint32_t w; int32_t ph; } ps_t;
static ps_t spL = {psb[0], 0, 0}, spR = {psb[1], 0, 0}, shimS = {psb[2], 0, 0};

static inline int32_t ps_rd(const ps_t *s, int32_t d)   /* the line d (16.16) samples ago */
{
    uint32_t pos = s->w - 1u - ((uint32_t)d >> 16);
    int32_t f = (d >> 8) & 255, a = s->buf[pos & (PSN - 1u)], b = s->buf[(pos - 1u) & (PSN - 1u)];
    return a + (((b - a) * f) >> 8);
}
/* rate: how far the delay moves per sample, 16.16. 0 = the same pitch, -65536 = an octave up, +32768 = a fifth down... */
static inline int32_t ps_run(ps_t *s, int32_t x, int32_t rate)
{
    int32_t d1, d2, w1, w2;
    s->buf[s->w & (PSN - 1u)] = (int16_t)cl15(x);
    s->w++;
    s->ph += rate;
    if (s->ph >= PSW) s->ph -= PSW;
    else if (s->ph < 0) s->ph += PSW;
    d1 = s->ph;
    d2 = d1 + (PSW >> 1);
    if (d2 >= PSW) d2 -= PSW;
    w1 = (d1 < (PSW >> 1) ? d1 : PSW - d1) >> 17;       /* a triangle: 0 at the ends of the window, 256 in the middle */
    w2 = 256 - w1;
    return (ps_rd(s, d1) * w1 + ps_rd(s, d2) * w2) >> 8;
}

static int32_t sp_rate;                         /* the echo's shift, 16.16 per sample; 0 = off */
static void spiral_tick(void)
{
    int32_t x = (int32_t)PE[P_SPIRAL] - 64;
    sp_rate = (x < 3 && x > -3) ? 0 : -x * 243;         /* +-4 semitones per repeat at the ends */
}

/* ---- vowel filter */
static const uint8_t VF[5][3] = {               /* the filter index (CUT_G) of F1 F2 F3 for A E I O U */
    {135, 150, 180}, {123, 170, 181}, {98, 178, 188}, {126, 140, 180}, {102, 142, 177}};
static struct { uint32_t a1, a2, a3; int32_t ic1, ic2; } vf[3];
static int32_t vw_talk;
static uint32_t swph;

static void vowel_tick(void)                    /* every 16 frames */
{
    uint32_t talk = PE[P_TALK], i;
    int32_t vp, seg, fr, shift, sweep = PE[P_SWEEP];
    vw_talk = (int32_t)talk;
    if (!talk) return;
    vp = (int32_t)PE[P_VOWEL] << 5;
    if (sweep) { swph += 467000u + (uint32_t)sweep * 36800u; vp += (sine(swph) * sweep) >> 11; }
    vp = vp < 0 ? 0 : vp > 4064 ? 4064 : vp;
    seg = vp >> 10; fr = vp & 1023;
    shift = (((int32_t)PE[P_SHIFT] - 64) * 3) >> 3;
    for (i = 0; i < 3u; i++) {
        int32_t ci = VF[seg][i] + (((int32_t)VF[seg + 1][i] - (int32_t)VF[seg][i]) * fr >> 10) + shift;
        uint32_t g, k = 11800u, den, a1;
        ci = ci < 0 ? 0 : ci > 255 ? 255 : ci;
        g = CUT_G[ci];
        den = 65536u + (uint32_t)(((uint64_t)g * (g + k)) >> 16);
        a1 = 0xFFFFFFFFu / den;
        a1 = a1 > 65535u ? 65535u : a1;
        vf[i].a1 = a1;
        vf[i].a2 = (uint32_t)(((uint64_t)g * a1) >> 16);
        vf[i].a3 = (uint32_t)(((uint64_t)g * vf[i].a2) >> 16);
    }
}
static inline int32_t vowel_frame(int32_t x)
{
    static const int32_t W[3] = {1050, 630, 390};        /* F1 is the strongest, F3 the weakest (Q8, with a boost: the bands are narrow) */
    int32_t out = 0, i;
    for (i = 0; i < 3; i++) {
        int32_t v3 = x - vf[i].ic2, v1, v2;
        v1 = (int32_t)(((int64_t)vf[i].a1 * vf[i].ic1 + (int64_t)vf[i].a2 * v3) >> 16);
        v2 = vf[i].ic2 + (int32_t)(((int64_t)vf[i].a2 * vf[i].ic1 + (int64_t)vf[i].a3 * v3) >> 16);
        vf[i].ic1 = 2 * v1 - vf[i].ic1;
        vf[i].ic2 = 2 * v2 - vf[i].ic2;
        vf[i].ic1 = vf[i].ic1 > (1 << 20) ? (1 << 20) : vf[i].ic1 < -(1 << 20) ? -(1 << 20) : vf[i].ic1;
        vf[i].ic2 = vf[i].ic2 > (1 << 20) ? (1 << 20) : vf[i].ic2 < -(1 << 20) ? -(1 << 20) : vf[i].ic2;
        out += (((v1 * 11800) >> 16) * W[i]) >> 8;      /* k * v1: a band-pass with a peak gain of one */
    }
    return (x * (127 - vw_talk) + cl15(out) * vw_talk) >> 7;
}
