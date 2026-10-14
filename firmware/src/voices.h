/* SPDX-License-Identifier: GPL-3.0-only */
/* The psychedelic voices and the reverb (audio ISR: integers only, no flash, no screen). Included by tripdeck.c after fx.h.
 *
 *   VOICE (the first knob of the OSC page) picks the engine; the other three knobs of that page change meaning with it:
 *     PAD    two detuned oscillators and a filter, with a slow tape wobble                                 PULSE W | DETUNE | SUB
 *     SITAR  a plucked string with a buzzing bridge (jawari) and a scooped start (meend), and a tamboura  BUZZ | MEEND | DRONE
 *            drone that hums Sa-Pa-Sa under the playing, tuned to the first note you play
 *     ORGAN  drawbar organ through a rotating speaker (Leslie)                                           DRAWBAR | LESLIE | 16 FOOT
 *     BELL   two-operator FM: glass bells and a soft electric piano                                       RATIO | INDEX | TINE
 *   The reverb is a big stereo feedback delay network; SHIMMER feeds an octave-up copy back into it, FREEZE holds the tail for ever.
 *   (The audio code reads PE, the parameters plus the drift of TRIP; the screen reads the same.) */

#define ENG() ((uint32_t)PE[P_VOICE] * 4u >> 7) /* 0 PAD, 1 SITAR, 2 ORGAN, 3 BELL */

static uint32_t note_inc(uint32_t note);        /* (below) */

/* ---- sitar: Karplus-Strong. A burst of noise goes round a delay line that loses highs and energy each trip; the bridge makes the
 * delay shorter the louder the string is, which is the buzz. */
#define KSN 2048u
static int16_t ksl[NV][KSN] __attribute__((section(".pool")));
static uint32_t ksr = 0x6D2B79F5u;

static void voice_sitar(voice_t *V, uint32_t v, uint32_t inc, uint32_t st, uint32_t fresh, int32_t ei, int32_t de, int32_t *mono)
{
    uint32_t per = 0xFFFFFFFFu / ((inc >> 8) | 1u), w = V->kw, inj = V->kinj, a = 24u + ((uint32_t)PE[P_CUT] * 231u >> 7), gl, i;
    int32_t comp, D, lps = V->klp, last = V->klast, sc = V->kscoop, bz = PE[P_O1], amp, velg;
    int16_t *line = ksl[v];

    per = per > (1900u << 8) ? (1900u << 8) : per < (4u << 8) ? (4u << 8) : per;       /* the period in samples, 8 fractional bits */
    if (fresh) {                                /* a new pluck: refill the string with noise over one period, start flat (meend) */
        inj = (per >> 8) + 2u;
        sc = (int32_t)(((per * (uint32_t)PE[P_O2] >> 7) * 18u) >> 7);
        lps = 0; last = 0;
    }
    sc -= (sc >> 8) + (sc > 0 ? 1 : 0);         /* the scoop dies away in about 0.1 s */
    sc = sc < 0 ? 0 : sc;
    comp = (int32_t)(((256u - a) * 256u) / a);  /* the loop filter delays the string a little: shorten the line to stay in tune */
    gl = 32768u - ((((127u - PE[P_DEC]) * 107u + 3277u) * (per >> 8)) * 3u >> 17);      /* energy kept per trip: DECAY sets how long it rings */
    if (st == ST_REL) gl = gl * 25u >> 5;       /* a lifted key damps the string */
    velg = 32767 - ((127 - (int32_t)V->vel) * 100);
    D = (int32_t)per - comp + sc;               /* (the scoop makes the string longer, so it starts flat and slides up) */

    for (i = 0; i < 16u; i++) {
        int32_t d = D - (((last < 0 ? -last : last) * bz) >> 13), x, r0, r1, nv;
        uint32_t pos;
        d = d < 1024 ? 1024 : d;
        pos = w - ((uint32_t)d >> 8);
        r0 = line[pos & (KSN - 1u)];
        r1 = line[(pos - 1u) & (KSN - 1u)];
        x = r0 + (((r1 - r0) * (d & 255)) >> 8);
        if (inj) { ksr ^= ksr << 13; ksr ^= ksr >> 17; ksr ^= ksr << 5; x = (int32_t)(ksr >> 16) - 32768; inj--; }
        lps += ((x - lps) * (int32_t)a) >> 8;   /* the string loses its highs; CUTOFF is how many */
        nv = (lps * (int32_t)gl) >> 15;
        nv = nv > 32767 ? 32767 : nv < -32767 ? -32767 : nv;
        line[w & (KSN - 1u)] = (int16_t)nv;
        w++;
        last = nv;
        ei += de;
        amp = ((ei >> 8) * velg) >> 15;
        mono[i] += (nv * amp) >> 15;
    }
    V->kw = w; V->kinj = inj; V->klp = lps; V->klast = last; V->kscoop = sc;
}

/* ---- organ: the drawbars are one table (two cycles of the 8 foot, so the 16 foot is harmonic 1), rebuilt in the main loop. */
#define ORGN 512u
static int16_t orgt[2][ORGN + 1u];
static const int16_t *volatile orgp = orgt[0];
static uint32_t org_cur, org_key = 0xFFFFFFFFu;

static void org_build(void)                     /* main loop: only when DRAWBAR or 16 FOOT moved */
{
    static const uint8_t HS[7] = {1, 2, 4, 6, 8, 10, 16};   /* 16' 8' 4' 2 2/3' 2' 1 3/5' 1' */
    uint32_t key = (uint32_t)PE[P_O1] << 8 | PE[P_O3], d = PE[P_O1], am[7], tot = 0, i, j, nxt = org_cur ^ 1u, sc;
    if (key == org_key) return;
    org_key = key;
    am[0] = PE[P_O3];
    am[1] = 128u;
    am[2] = d * 2u > 128u ? 128u : d * 2u;
    am[3] = d > 32u ? ((d - 32u) * 2u > 110u ? 110u : (d - 32u) * 2u) : 0u;
    am[4] = d > 56u ? ((d - 56u) * 2u > 100u ? 100u : (d - 56u) * 2u) : 0u;
    am[5] = d > 80u ? ((d - 80u) * 3u > 80u ? 80u : (d - 80u) * 3u) : 0u;
    am[6] = d > 100u ? ((d - 100u) * 4u > 70u ? 70u : (d - 100u) * 4u) : 0u;
    for (j = 0; j < 7u; j++) tot += am[j];
    sc = (28000u << 8) / tot;
    for (i = 0; i < ORGN; i++) {
        int32_t acc = 0;
        for (j = 0; j < 7u; j++) acc += (int32_t)am[j] * sine((uint32_t)(i * HS[j]) << 23);
        orgt[nxt][i] = (int16_t)(((int64_t)acc * (int32_t)sc) >> 23);
    }
    orgt[nxt][ORGN] = orgt[nxt][0];
    org_cur = nxt;
    orgp = orgt[nxt];
}

static void voice_organ(voice_t *V, uint32_t inc, int32_t ei, int32_t de, int32_t *mono)
{
    const int16_t *tab = orgp;
    uint32_t ps = V->ps, ih = inc >> 1, i;
    int32_t velg = 32767 - ((127 - (int32_t)V->vel) * 100);
    for (i = 0; i < 16u; i++) {
        int32_t t0, t1, s, amp;
        uint32_t idx;
        ps += ih;
        idx = ps >> 23;
        t0 = tab[idx]; t1 = tab[idx + 1u];
        s = t0 + (((t1 - t0) * (int32_t)((ps >> 15) & 255u)) >> 8);
        ei += de;
        amp = ((ei >> 8) * velg) >> 15;
        mono[i] += (s * amp) >> 15;
    }
    V->ps = ps;
}

/* ---- bell: two-operator FM. The modulator's depth follows the filter envelope (it decays with DECAY), so a note starts bright and
 * mellows; TINE adds a quick high strike (a third operator at 14 times the pitch) for the hammer of an electric piano. */
static void voice_bell(voice_t *V, uint32_t inc, int32_t ei, int32_t de, int32_t *mono)
{
    uint32_t pa = V->pa, pb = V->pb, ps = V->ps, i, rat = 128u + (uint32_t)PE[P_O1] * 16u;
    uint32_t incm = (uint32_t)(((uint64_t)inc * rat) >> 8), inct = inc * 14u;
    int32_t fz = V->fenv >> 8, velg = 32767 - ((127 - (int32_t)V->vel) * 100), idxp, tidxp, fzs = fz >> 7;
    idxp = (((int32_t)PE[P_O2] * 1300) * fzs) >> 8;
    tidxp = (((int32_t)PE[P_O3] * 900) * ((fzs * fzs) >> 8)) >> 8;
    for (i = 0; i < 16u; i++) {
        uint32_t ph;
        int32_t amp;
        pa += inc; pb += incm;
        ph = pa + (uint32_t)((int64_t)sine(pb) * idxp);
        if (tidxp) { ps += inct; ph += (uint32_t)((int64_t)sine(ps) * tidxp); }
        ei += de;
        amp = ((ei >> 8) * velg) >> 15;
        mono[i] += (sine(ph) * amp) >> 15;
    }
    V->pa = pa; V->pb = pb; V->ps = ps;
}

/* ---- the tamboura: four strings (Sa, Pa, Sa', Sa' a hair sharp), sawtooth, through a filter that slowly breathes */
#define DRT 22050u                              /* it keeps humming 8 s after the last note */
static uint32_t dr_t, dph[4], dinc[4], dlf;
static int32_t dr_lvl, dr_lp, dr_a = 40;

static void drone_note(uint32_t note)           /* the first note after silence names Sa */
{
    uint32_t i0 = note_inc(36u + note % 12u);
    dinc[0] = i0; dinc[1] = i0 + (i0 >> 1); dinc[2] = i0 << 1; dinc[3] = (i0 << 1) + (i0 >> 8);
}
static void drone_tick(uint32_t on)             /* every 16 frames */
{
    int32_t tgt = on && dr_t ? (int32_t)PE[P_O3] << 8 : 0, dd = tgt - dr_lvl, stp = dd >> 11;
    if (dr_t) dr_t--;
    if (!stp) stp = dd > 0 ? 1 : dd < 0 ? -1 : 0;
    dr_lvl += stp;
    dlf += 140000u;
    dr_a = 16 + ((32768 + sine(dlf)) >> 10);
}
static inline int32_t drone_sample(void)
{
    int32_t x = 0, k;
    if (!dr_lvl) return 0;
    for (k = 0; k < 4; k++) { dph[k] += dinc[k]; x += (int32_t)(dph[k] >> 16) - 32768; }
    dr_lp += (((x >> 2) - dr_lp) * dr_a) >> 8;
    return (dr_lp * dr_lvl) >> 17;
}

/* ---- the Leslie: a delay swung by the rotating horn (Doppler), and its level (the horn turns away), a quarter turn apart for
 * left and right; the drum adds a slower level swirl. The speed glides up and down, as a real rotor does. */
static int16_t lsl[256];
static uint32_t lw, hph, dph2;
static int32_t hinc, drinc, tdL = 5632, tdR = 5632, cdL = 5632, cdR = 5632, lgL = 27000, lgR = 27000;

static void leslie_tick(uint32_t les)           /* every 16 frames; les 0 = off */
{
    int32_t ht = les ? 1090785 + (int32_t)les * 77300 : 0, sL, sR, sd;
    hinc += (ht - hinc) >> 11;
    drinc += ((ht - (ht >> 2)) - drinc) >> 13;
    hph += (uint32_t)hinc; dph2 += (uint32_t)drinc;
    sL = sine(hph); sR = sine(hph + 0x40000000u); sd = sine(dph2);
    tdL = 5632 + ((sL * 3584) >> 15);
    tdR = 5632 + ((sR * 3584) >> 15);
    lgL = 27000 + ((sL * 5000) >> 15) + ((sd * 2500) >> 15);
    lgR = 27000 + ((sR * 5000) >> 15) - ((sd * 2500) >> 15);
}
static inline void leslie_frame(int32_t m, int32_t *pL, int32_t *pR)
{
    int32_t pl, pr, a0, a1, b0, b1;
    uint32_t il, ir;
    lsl[lw] = (int16_t)(m > 32767 ? 32767 : m < -32767 ? -32767 : m);
    cdL += (tdL - cdL) >> 6; cdR += (tdR - cdR) >> 6;
    pl = (int32_t)(lw << 8) - cdL; pr = (int32_t)(lw << 8) - cdR;
    il = (uint32_t)(pl >> 8) & 255u; ir = (uint32_t)(pr >> 8) & 255u;
    a0 = lsl[il]; a1 = lsl[(il + 1u) & 255u]; b0 = lsl[ir]; b1 = lsl[(ir + 1u) & 255u];
    *pL = ((a0 + (((a1 - a0) * (pl & 255)) >> 8)) * lgL) >> 15;
    *pR = ((b0 + (((b1 - b0) * (pr & 255)) >> 8)) * lgR) >> 15;
    lw = (lw + 1u) & 255u;
}

/* ---- the pad's tape wobble: two slow sines of different speeds nudge every pitch by a few cents */
static uint32_t wph1, wph2;
static int32_t wow;
static void wow_tick(void)
{
    wph1 += 545000u; wph2 += 1510000u;          /* 0.35 Hz and 0.97 Hz */
    wow = ((sine(wph1) * 2400) >> 15) + ((sine(wph2) * 1600) >> 15);
}

/* ---- the reverb: four delay lines mixed through a Hadamard matrix (a feedback delay network), highs damped in the loop; the
 * input goes in as a mono sum, the two outputs are two orthogonal mixes of the lines, so it is wide. Tail about 3 s.
 * SHIMMER: the lines' average goes through an octave-up shifter and back in. FREEZE: no more input, no damping, feedback just under 1. */
#define RVN 4096u
static int16_t rvl[4][RVN] __attribute__((section(".pool")));
static const uint16_t RVLEN[4] = {1613, 2053, 2593, 3217};
static uint32_t rvw;
static int32_t rvlp[4], rv_g = 232, rv_lp = 156, rv_in = 256, rv_sh, rv_mix;

static void reverb_tick(void)                   /* every 16 frames */
{
    int32_t fz = PE[P_FREEZE] >= 64u, tin = fz ? 0 : 256;
    rv_g = fz ? 255 : 232;
    rv_lp = fz ? 256 : 250 - (((int32_t)PE[P_DAMP] * 150) >> 7);
    rv_in += (tin - rv_in) >> 3;
    if (!tin && rv_in < 8) rv_in = 0;
    rv_sh = fz ? 0 : (int32_t)PE[P_SHIM];
    rv_mix = fz && PE[P_RVB] < 100u ? 100 : (int32_t)PE[P_RVB];
}

static inline int32_t rclip(int32_t v) { return v > 32767 ? 32767 : v < -32767 ? -32767 : v; }

static inline void reverb_frame(int32_t *pL, int32_t *pR)
{
    int32_t r0, r1, r2, r3, a, b, c, d, in = (((*pL + *pR) >> 2) * rv_in) >> 8;
    uint32_t w = rvw;
    r0 = rvl[0][(w - RVLEN[0]) & (RVN - 1u)]; rvlp[0] += ((r0 - rvlp[0]) * rv_lp) >> 8; r0 = rvlp[0];
    r1 = rvl[1][(w - RVLEN[1]) & (RVN - 1u)]; rvlp[1] += ((r1 - rvlp[1]) * rv_lp) >> 8; r1 = rvlp[1];
    r2 = rvl[2][(w - RVLEN[2]) & (RVN - 1u)]; rvlp[2] += ((r2 - rvlp[2]) * rv_lp) >> 8; r2 = rvlp[2];
    r3 = rvl[3][(w - RVLEN[3]) & (RVN - 1u)]; rvlp[3] += ((r3 - rvlp[3]) * rv_lp) >> 8; r3 = rvlp[3];
    if (rv_sh) in += (ps_run(&shimS, (r0 + r1 + r2 + r3) >> 2, -65536) * rv_sh * 11) >> 13;   /* (a gain of 0.17 at the top: above 0.19 the tail grows without end) */
    a = r0 + r1; b = r0 - r1; c = r2 + r3; d = r2 - r3;
    rvl[0][w] = (int16_t)rclip(((a + c) * rv_g >> 9) + in);
    rvl[1][w] = (int16_t)rclip(((b + d) * rv_g >> 9) + in);
    rvl[2][w] = (int16_t)rclip(((a - c) * rv_g >> 9) - in);
    rvl[3][w] = (int16_t)rclip(((b - d) * rv_g >> 9) + in);
    rvw = (w + 1u) & (RVN - 1u);
    *pL += ((r0 + r1 + r2 - r3) >> 1) * rv_mix >> 7;
    *pR += ((r0 - r1 + r2 + r3) >> 1) * rv_mix >> 7;
}
