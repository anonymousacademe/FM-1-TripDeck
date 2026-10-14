/* SPDX-License-Identifier: GPL-3.0-only */
/* TRIPDECK: a four-voice subtractive synth with five psychedelic scenes, for the M-VAVE FM-1.
 * The synth, arpeggiator, sequencer and patch mutator are CyberDeck's. On top of them: the buttons EDIT ENV LFO FX GLO are scenes.
 * Each puts a full-screen psychedelic picture on the display AND an effect on the sound; the four knobs change both at once.
 *
 *   EDIT  KALEIDO  mandala / ring modulator       ENV  TUNNEL  spiral tunnel / phaser     LFO  PLASMA  liquid colour / chorus
 *   FX    MELT     video feedback / tape echo     GLO  WARP    hyperspace / crush and rate warble
 *   knobs 1-4      always: shape, depth, motion, colour (the labels on the screen say what each does in that scene)
 *   SEL            back to the synth pages (OSC FLT ENV LFO FX GLO, one press each);  the same scene button again: leave the scene
 *   ARP SEQ PLAY REC OCT- OCT+ SAVE HOME   as in CyberDeck (HOME in a scene: surprise: it randomises the four knobs)
 *   ALGORITHM / SELECT / PRESETS   cutoff / resonance / patch, in every mode
 *
 * Built on CyberDeck and Dinghy (zvenson); the platform is Felucca (Leo Kuroshita, Hugelton Instruments) and SLOOP (isod89).
 * Integers only (no float, no 64-bit division). Pictures: scene_vis.h (main loop). Sound of the scenes: scene_audio.h (audio ISR). */
#include "platform.c"
#include "tables.h"

#ifndef FM1_TICKS_PER_US
#define FM1_TICKS_PER_US 24u
#endif

/* ---- parameters: 6 pages x 4 knobs, each 0..127 */
enum { P_MIX, P_PW, P_DET, P_SUB,
       P_CUT, P_RES, P_ENVAMT, P_DRV,
       P_ATK, P_DEC, P_SUS, P_REL,
       P_LRATE, P_LDEPTH, P_LDEST, P_LSHAPE,
       P_CRUSH, P_DMIX, P_DTIME, P_DFB,
       P_GLIDE, P_MODE, P_TUNE, P_VELO,        /* ---- the first 24 belong to the preset; the rest are global ---- */
       P_ARPMODE, P_ARPOCT, P_ARPRATE, P_GATE,
       P_BPM, P_SWING, P_LEN, P_XPOSE,
       NP };
#define NPP 24u                                 /* params per preset */
static volatile uint8_t P[NP];

#define NPRESET 8u
static const uint8_t GLOBAL0[NP - NPP] = {0, 20, 40, 80,   60, 0, 60, 64};   /* up, 1 oct, 1/16, gate 67 % | 120 bpm, no swing, 16 steps, +0 */
static const uint8_t FACTORY[NPRESET][NPP] = {
    /*        MIX PW DET SUB | CUT RES ENV DRV | ATK DEC SUS REL | LRT LDP LDS LSH | CRU DMX DTM DFB | GLD MOD TUN VEL */
    /* NEON RAIN   */ { 50, 64, 20, 30,   70, 30, 76, 20,   85, 90, 80,100,   25, 30, 10,  0,    0, 50, 70, 60,    0,  0, 64, 40},
    /* CHROME BASS */ { 20, 50, 10,100,   42, 70,110, 60,    0, 62, 20, 40,    0,  0, 10,  0,    0,  0, 60, 20,   40,100, 64, 60},
    /* KANEDA      */ { 60, 40, 35, 20,   80, 40, 90, 50,    2, 70, 90, 50,   90, 25, 64,  0,    0, 40, 80, 55,   20,100, 64, 50},
    /* ZAIBATSU    */ { 64, 90, 12, 80,   36, 90,100, 80,   30,100, 40,105,   20, 70, 10,  0,   20, 70,100, 80,    0,  0, 64, 30},
    /* GLITCH ICE  */ { 80, 30,  5,  0,   60, 80,110, 40,    0, 50,  0, 40,  105, 60, 10,100,   70, 60, 40, 70,    0,  0, 64, 50},
    /* TYRELL      */ { 40, 64, 25, 40,   55, 25,100, 30,   55, 80,100, 70,   85,  8, 64,  0,    0, 45, 95, 55,    0,  0, 64, 50},
    /* WIRED       */ { 30, 64, 40, 20,   50, 60,105, 25,    0, 58,  0, 50,    0,  0, 10,  0,    0, 55, 60, 65,    0,  0, 64, 60},
    /* DATA MOSH   */ {100, 70, 90, 70,   45,100, 80,100,    0, 90, 90, 30,   70, 90, 10, 64,   90, 30, 50, 40,   30,100, 64, 40},
};
static const char *const PNAME[NPRESET] = {"NEON RAIN", "CHROME BASS", "KANEDA", "ZAIBATSU",
                                           "GLITCH ICE", "TYRELL", "WIRED", "DATA MOSH"};
static const char *const LABEL[NP] = {
    "MIX", "PULSE W", "DETUNE", "SUB",
    "CUTOFF", "RESO", "ENV AMT", "DRIVE",
    "ATTACK", "DECAY", "SUSTAIN", "RELEASE",
    "RATE", "DEPTH", "DEST", "SHAPE",
    "CRUSH", "DLY MIX", "DLY TIME", "FEEDBACK",
    "GLIDE", "MODE", "TUNE", "VELOCITY",
    "ARP MODE", "OCTAVES", "RATE", "GATE",
    "BPM", "SWING", "LENGTH", "TRANSPOSE"};
static const char *const TAB[8] = {"OSC", "FLT", "ENV", "LFO", "FX", "GLO", "ARP", "SEQ"};

/* ---- the sound (runs in the audio ISR: keep it short, no flash, no screen) */
#define NV 4u
#define ONE (1 << 23)                           /* envelope 1.0 (Q23) */
enum { ST_OFF, ST_ATK, ST_DEC, ST_REL };

typedef struct {
    volatile uint32_t inc_t;                    /* target phase step (main -> ISR) */
    volatile uint8_t gate, trig, vel, note;
    volatile uint8_t stage;                     /* ISR writes, main reads */
    uint8_t seen;
    uint32_t age, inc, pa, pb, ps;              /* phase steps / accumulators */
    int32_t env, fenv, ic1, ic2;
} voice_t;
static voice_t voice[NV];

#define SCOPE_N 2048u
static int16_t scope[SCOPE_N];
static volatile uint32_t scope_w, cpu_pct;

#define DLEN 32768u
static int16_t dlL[DLEN] __attribute__((section(".pool")));
static int16_t dlR[DLEN] __attribute__((section(".pool")));

static inline int32_t sclip(int32_t v)          /* soft clip, 1.5v - 0.5v^3 */
{
    int32_t t;
    if (v > 32767) return 32767;
    if (v < -32767) return -32767;
    t = (v * v) >> 15;
    return (v * ((98304 - t) >> 1)) >> 15;
}

#include "scene_audio.h"
static void engine_tick(void);

static void app_audio(int32_t *out, uint32_t n)
{
    static uint32_t lph, rnd = 0x1234567u, dw, crush_n;
    static int32_t lfo, sh, dt_cur = 20000, lpL, lpR, held, pk, lgain = 32768;
    static uint32_t cpu_max, cpu_blk;
    uint32_t t0 = fm1_ticks(), b, v, i;

    for (b = 0; b < n; b += 16u) {
        int32_t mono[16], m, dtt;
        uint32_t prev_lph = lph, shape = P[P_LSHAPE] >> 5, dest = (uint32_t)P[P_LDEST] * 3u >> 7;
        uint32_t mono_mode = P[P_MODE] >= 64u;
        int32_t ldep = P[P_LDEPTH];

        /* LFO, once per tick */
        lph += LFO_INC[P[P_LRATE]];
        if (lph < prev_lph) { rnd ^= rnd << 13; rnd ^= rnd >> 17; rnd ^= rnd << 5; sh = (int32_t)(rnd >> 16) - 32768; }
        {
            int32_t s = (int32_t)lph >> 16;
            lfo = shape == 0u ? ((s < 0 ? -s : s) << 1) - 32767 : shape == 1u ? s : shape == 2u ? (s < 0 ? -32767 : 32767) : sh;
            lfo = (lfo * ldep) >> 7;            /* depth applied: +-32512 */
        }

        engine_tick();                          /* events, arpeggiator, sequencer: they start and stop voices */
        scene_tick();                           /* the scene effect: its LFO, its coefficients, and what it takes over in the delay */
        for (i = 0; i < 16u; i++) mono[i] = 0;

        for (v = 0; v < NV; v++) {
            voice_t *V = &voice[v];
            uint32_t st = V->stage, inc, incb, pa, pb, ps, wph, a1, a2, a3, g, k, gk, den;
            int32_t env, ei, de, fenv, ic1, ic2, velg, ci, pwq, dg, sublvl, mixa, mixb;
            uint32_t gate = V->gate;

            if (V->trig != V->seen) {           /* a new note */
                V->seen = V->trig;
                if (st == ST_OFF) { V->pa = V->pb = V->ps = 0; V->ic1 = V->ic2 = 0; }
                st = ST_ATK;
                V->fenv = ONE;
                if (!mono_mode || P[P_GLIDE] == 0u) V->inc = V->inc_t;
            }
            if (st == ST_OFF) continue;
            if (!gate && (st == ST_ATK || st == ST_DEC)) st = ST_REL;

            /* control rate: glide, envelopes */
            inc = V->inc;
            if (mono_mode && P[P_GLIDE])
                inc = (uint32_t)((int32_t)inc + (int32_t)(((int64_t)((int32_t)(V->inc_t - inc)) * GLIDE_R[P[P_GLIDE]]) >> 16));
            else
                inc = V->inc_t;
            V->inc = inc;

            env = V->env;
            {
                int32_t pe = env, sus = (int32_t)P[P_SUS] * 66052;
                if (st == ST_ATK) {
                    env += (int32_t)(((int64_t)(ONE + ONE / 3 - env) * RATE16[P[P_ATK]]) >> 24);
                    if (env >= ONE) { env = ONE; st = ST_DEC; }
                } else if (st == ST_DEC) {
                    env += (int32_t)(((int64_t)(sus - env) * RATE16[P[P_DEC]]) >> 24);
                    if (sus < (1 << 12) && env < (1 << 12)) st = ST_OFF;
                } else {
                    int32_t d = (int32_t)(((int64_t)env * RATE16[P[P_REL]]) >> 24);
                    env -= d < 1 ? 1 : d;
                    if (env < (1 << 12)) { env = 0; st = ST_OFF; }
                }
                ei = pe;
                de = (env - pe) >> 4;
            }
            V->env = env;
            V->stage = (uint8_t)st;
            fenv = V->fenv;
            fenv -= (int32_t)(((int64_t)fenv * RATE16[P[P_DEC]]) >> 24);
            V->fenv = fenv;

            /* oscillator steps with the LFO */
            if (dest == 1u) inc += (int32_t)(((int64_t)inc * lfo) >> 18);
            incb = inc + (uint32_t)(((uint64_t)inc * P[P_DET] * 26u) >> 16);
            pwq = ((int32_t)P[P_PW] - 64) * 3;
            if (dest == 2u) pwq += lfo >> 8;
            pwq = pwq > 240 ? 240 : pwq < -240 ? -240 : pwq;
            wph = 0x80000000u + (uint32_t)(pwq * (1 << 23));
            mixb = P[P_MIX]; mixa = 127 - mixb;
            sublvl = P[P_SUB];

            /* filter coefficients (Simper SVF) */
            ci = (int32_t)P[P_CUT] * 2 + (int32_t)(((int64_t)(fenv >> 8) * ((int32_t)P[P_ENVAMT] - 64)) >> 14) + ((int32_t)V->note - 60);
            if (dest == 0u) ci += (lfo * 96) >> 15;
            ci = ci < 0 ? 0 : ci > 255 ? 255 : ci;
            g = CUT_G[ci];
            k = 131072u - (uint32_t)P[P_RES] * 980u;
            gk = (uint32_t)(((uint64_t)g * (g + k)) >> 16);
            den = 65536u + gk;
            a1 = 0xFFFFFFFFu / den;
            if (a1 > 65535u) a1 = 65535u;
            a2 = (uint32_t)(((uint64_t)g * a1) >> 16);
            a3 = (uint32_t)(((uint64_t)g * a2) >> 16);
            dg = 256 + (int32_t)P[P_DRV] * 16;
            velg = 32767 - ((127 - (int32_t)V->vel) * (int32_t)P[P_VELO] * 2);

            pa = V->pa; pb = V->pb; ps = V->ps; ic1 = V->ic1; ic2 = V->ic2;
            for (i = 0; i < 16u; i++) {
                int32_t x, v3, v1, v2, y, sa, sb, ss, amp;
                pa += inc; pb += incb; ps += inc >> 1;
                sa = (int32_t)(pa >> 16) - 32768;
                sb = pb < wph ? 32767 : -32767;
                ss = (ps >> 31) ? 32767 : -32767;
                x = (((sa * mixa + sb * mixb) >> 7) * 22 >> 5) + ((ss * sublvl) >> 9);

                v3 = x - ic2;
                v1 = (int32_t)(((int64_t)a1 * ic1 + (int64_t)a2 * v3) >> 16);
                v2 = ic2 + (int32_t)(((int64_t)a2 * ic1 + (int64_t)a3 * v3) >> 16);
                ic1 = 2 * v1 - ic1;
                ic2 = 2 * v2 - ic2;
                ic1 = ic1 > (1 << 20) ? (1 << 20) : ic1 < -(1 << 20) ? -(1 << 20) : ic1;
                ic2 = ic2 > (1 << 20) ? (1 << 20) : ic2 < -(1 << 20) ? -(1 << 20) : ic2;

                y = sclip((v2 * dg) >> 8);
                ei += de;
                amp = ((ei >> 8) * velg) >> 15;
                mono[i] += (y * amp) >> 15;
            }
            V->pa = pa; V->pb = pb; V->ps = ps; V->ic1 = ic1; V->ic2 = ic2;
        }

        /* bit crusher, ping-pong delay, out */
        dtt = e_dt;
        {
            int32_t d = dtt - dt_cur;
            d = d >> 6;
            if (d == 0 && dtt != dt_cur) d = dtt > dt_cur ? 1 : -1;
            dt_cur += d;
        }
        {                                       /* limiter gain: down at once, back up slowly; threshold about -2 dBFS */
            int32_t target = pk > 26000 ? (int32_t)(26000u * 32768u / (uint32_t)pk) : 32768;
            lgain = target < lgain ? target : lgain + ((target - lgain) >> 4);
        }
        for (i = 0; i < 16u; i++) {
            int32_t dry, yL, yR, fL, fR, L, R, wd;
            uint32_t rd;
            m = mono[i] >> 1;
            if (P[P_CRUSH]) {
                uint32_t bits = 16u - ((uint32_t)P[P_CRUSH] * 12u >> 7), hold = 1u + ((uint32_t)P[P_CRUSH] * 10u >> 7);
                if (++crush_n >= hold) { crush_n = 0; held = m & ~((1 << (16u - bits)) - 1); }
                m = held;
            }
            dry = m;
            rd = (dw - (uint32_t)(dt_cur + e_wow)) & (DLEN - 1u);
            yL = dlL[rd]; yR = dlR[rd];
            lpL += (yL - lpL) >> e_lpsh; lpR += (yR - lpR) >> e_lpsh;
            fL = (lpL * e_dfb) >> 7;
            fR = (lpR * e_dfb) >> 7;
            wd = dry + fR;                      /* ping-pong: input and the right repeat feed the left, left feeds right */
            dlL[dw] = (int16_t)(wd > 32767 ? 32767 : wd < -32767 ? -32767 : wd);
            dlR[dw] = (int16_t)(fL > 32767 ? 32767 : fL < -32767 ? -32767 : fL);
            dw = (dw + 1u) & (DLEN - 1u);
            L = dry + ((yL * e_dmix) >> 7);
            R = dry + ((yR * e_dmix) >> 7);
            scene_frame(&L, &R);                /* the effect of the scene, after the patch's own */
            {                                   /* limiter: peak follower in, gain per tick below; the soft clip catches the rest */
                int32_t pkn = (L < 0 ? -L : L) > (R < 0 ? -R : R) ? (L < 0 ? -L : L) : (R < 0 ? -R : R);
                pk = pkn > pk ? pkn : pk - (pk >> 11);
            }
            L = sclip((L * lgain) >> 15);
            R = sclip((R * lgain) >> 15);
            out[2u * (b + i)] += L;
            out[2u * (b + i) + 1u] += R;
            scope[scope_w++ & (SCOPE_N - 1u)] = (int16_t)((L + R) >> 1);
        }
    }

    vis_pk = (uint32_t)pk;                      /* for the picture */
    {   /* CPU: the worst block of the last 64, in percent of 256 frames (5805 us) */
        uint32_t dt = fm1_ticks() - t0;
        if (dt > cpu_max) cpu_max = dt;
        if (++cpu_blk >= 64u) { cpu_pct = cpu_max / (5805u * FM1_TICKS_PER_US / 100u); cpu_max = 0; cpu_blk = 0; }
    }
}

/* ---- notes and the engine. Everything that starts or stops a voice runs in the audio ISR (one writer for the voices,
 * and a clock that is exact to a 16-sample tick). The main loop (keys, MIDI) only posts events. */
static const uint32_t INC_C9[12] = {            /* MIDI 120..131; lower octaves shift right */
    815363807u, 863847862u, 915214929u, 969636441u, 1027294024u, 1088380105u,
    1153098554u, 1221665363u, 1294309365u, 1371273005u, 1452813141u, 1539201906u};
static uint32_t ages;
static uint8_t stk[8], nstk;

static uint32_t note_inc(uint32_t note)
{
    uint32_t inc = INC_C9[note % 12u] >> (10u - note / 12u);
    int32_t tune = (int32_t)P[P_TUNE] - 64;
    return inc + (uint32_t)(int32_t)(((int64_t)inc * (tune * 28)) >> 16);
}
static int is_mono(void) { return P[P_MODE] >= 64u; }

static void voice_on(uint32_t note, uint32_t vel)
{
    uint32_t v, pick = 0, best = 0xFFFFFFFFu, i, j;
    if (note > 127u || !vel) return;
#ifdef DINGHY_HOST
    printf("%6u ms  voice on  %3u vel %3u\n", fm1_ms, note, vel);   /* (host only: the trace the tests read) */
#endif
    if (is_mono()) {
        for (i = j = 0; i < nstk; i++) if (stk[i] != note) stk[j++] = stk[i];
        nstk = (uint8_t)j;
        if (nstk < 8u) stk[nstk++] = (uint8_t)note;
        voice[0].note = (uint8_t)note;
        voice[0].vel = (uint8_t)vel;
        voice[0].inc_t = note_inc(note);
        if (!voice[0].gate) voice[0].trig++;    /* legato: no retrigger while a key is held */
        voice[0].gate = 1;
        return;
    }
    for (v = 0; v < NV; v++) {                  /* same note, else a free voice, else a releasing one, else the oldest */
        uint32_t score = voice[v].note == note && voice[v].gate ? 0u
                       : voice[v].stage == ST_OFF ? 1u
                       : !voice[v].gate ? 2u + ((uint32_t)voice[v].env >> 12)
                       : 0x80000000u + voice[v].age;
        if (score < best) { best = score; pick = v; }
    }
    voice[pick].note = (uint8_t)note;
    voice[pick].vel = (uint8_t)vel;
    voice[pick].age = ++ages;
    voice[pick].inc_t = note_inc(note);
    voice[pick].gate = 1;
    voice[pick].trig++;
}
static void voice_off(uint32_t note)
{
    uint32_t v, i, j;
    if (is_mono()) {
        for (i = j = 0; i < nstk; i++) if (stk[i] != note) stk[j++] = stk[i];
        nstk = (uint8_t)j;
        if (nstk) {                             /* back to the previous held key, legato */
            voice[0].note = stk[nstk - 1u];
            voice[0].inc_t = note_inc(stk[nstk - 1u]);
        } else
            voice[0].gate = 0;
        return;
    }
    for (v = 0; v < NV; v++)
        if (voice[v].note == note) voice[v].gate = 0;
}
static void voice_panic(void)
{
    uint32_t v;
    nstk = 0;
    for (v = 0; v < NV; v++) voice[v].gate = 0;
}
static int quiet(void)
{
    uint32_t v;
    for (v = 0; v < NV; v++) if (voice[v].stage != ST_OFF) return 0;
    return 1;
}

/* main -> ISR: events and switches. ISR -> main: the transport. One writer for each. */
enum { EV_OFF, EV_ON, EV_PANIC, EV_REST };
#define EVQ 64u
static volatile uint16_t evq[EVQ];
static volatile uint32_t evq_w, evq_r;
static volatile uint8_t want_play, rec_arm, arp_cmd;     /* arp_cmd: 0 off, 1 on, 2 hold */
static volatile uint8_t pat[32];                         /* a step: 0 rest, else note (+ 0x80: accent) */
static volatile uint32_t playing, seq_step, rec_cursor;

static void push_ev(uint32_t type, uint32_t note, uint32_t vel)
{
    uint32_t w = evq_w;
    if (w - evq_r >= EVQ) return;
    evq[w & (EVQ - 1u)] = (uint16_t)(type << 14 | (vel & 127u) << 7 | (note & 127u));
    evq_w = w + 1u;
}

static uint32_t pmask[4], lmask[4];             /* keys down / held by HOLD, one bit per note */
static uint8_t alist[32];
static uint32_t an, aidx, arp_acc, arp_cur, arp_off_at, seq_acc, seq_cur, seq_off_at, seq_cur_len, seq_len = 16;
static uint32_t eng_rnd = 0x9E3779B9u, rec_fresh, prev_rec, prev_arp, arp_dirty, octs_seen = 9, an_prev;
static int32_t adir = 1, xp_cache;

static uint32_t rand32(void) { eng_rnd ^= eng_rnd << 13; eng_rnd ^= eng_rnd >> 17; eng_rnd ^= eng_rnd << 5; return eng_rnd; }
static uint32_t clamp_note(int32_t n) { return n < 1 ? 1u : n > 127 ? 127u : (uint32_t)n; }

static void arp_build(void)                     /* the notes of the chord, low to high, across the octave range */
{
    const uint32_t *src = arp_cmd == 2u ? lmask : pmask;
    uint32_t octs = 1u + ((uint32_t)P[P_ARPOCT] * 3u >> 7), o, nt, n = 0;
    for (o = 0; o < octs; o++)
        for (nt = 0; nt < 128u && n < 32u; nt++)
            if ((src[nt >> 5] >> (nt & 31u) & 1u) && nt + 12u * o <= 127u) alist[n++] = (uint8_t)(nt + 12u * o);
    an = n;
    octs_seen = octs;
    if (aidx >= an) aidx = 0;
}

static void arp_fire(void)
{
    uint32_t mode = P[P_ARPMODE] >> 5, pos = aidx;
    if (arp_cur) { voice_off(arp_cur); arp_cur = 0; }
    if (!an) return;
    if (pos >= an) pos = 0;
    arp_cur = alist[pos];
    voice_on(arp_cur, 100);
    if (an < 2u) { aidx = 0; return; }
    if (mode == 0u) aidx = pos + 1u >= an ? 0 : pos + 1u;                              /* up */
    else if (mode == 1u) aidx = pos == 0 ? an - 1u : pos - 1u;                         /* down */
    else if (mode == 2u) {                                                             /* up-down */
        int32_t nx = (int32_t)pos + adir;
        if (nx >= (int32_t)an) { nx = (int32_t)an - 2; adir = -1; }
        else if (nx < 0) { nx = 1; adir = 1; }
        aidx = (uint32_t)nx;
    } else aidx = rand32() % an;                                                       /* random */
}

static void rec_note(uint32_t note, uint32_t vel)
{
    uint8_t code = (uint8_t)(clamp_note((int32_t)note - xp_cache) | (vel >= 110u ? 0x80u : 0u));
    uint32_t i;
    if (playing) {                              /* overdub: to the nearest step */
        uint32_t s = seq_step;
        if (seq_acc * 2u >= seq_cur_len) s = s + 1u >= seq_len ? 0 : s + 1u;
        pat[s] = code;
    } else {                                    /* step record: the next step */
        if (rec_fresh) { for (i = 0; i < 32u; i++) pat[i] = 0; rec_fresh = 0; }
        if (rec_cursor < 32u) { pat[rec_cursor] = code; rec_cursor++; }
    }
}

static void user_on(uint32_t note, uint32_t vel)
{
    if (arp_cmd == 2u && !(pmask[0] | pmask[1] | pmask[2] | pmask[3]))
        lmask[0] = lmask[1] = lmask[2] = lmask[3] = 0;            /* HOLD: a new chord replaces the old one */
    pmask[note >> 5] |= 1u << (note & 31u);
    lmask[note >> 5] |= 1u << (note & 31u);
    arp_dirty = 1;
    if (rec_arm) rec_note(note, vel);
    if (!arp_cmd) voice_on(note, vel);
}
static void user_off(uint32_t note)
{
    pmask[note >> 5] &= ~(1u << (note & 31u));
    arp_dirty = 1;
    if (!arp_cmd) voice_off(note);
}

static void engine_tick(void)                   /* every 16 frames */
{
    uint32_t step_len, arp_len, gl, bpm, swd, ac, wp, ra, i;
    static const uint8_t ARPK[4] = {32, 16, 11, 8};             /* 1/8, 1/16, 1/16 triplet, 1/32 in 16ths of a step */

    bpm = 60u + P[P_BPM];
    step_len = 661500u / bpm;                   /* samples per 16th note: 44100 * 60 / 4 / bpm */
    swd = (step_len * P[P_SWING]) >> 9;         /* the odd steps come late, up to a quarter step */
    arp_len = (step_len * ARPK[P[P_ARPRATE] >> 5]) >> 4;
    gl = 16u + ((uint32_t)P[P_GATE] * 112u >> 7);               /* gate 12..99 % in 128ths */
    seq_len = 1u + (P[P_LEN] >> 2);
    xp_cache = (int32_t)P[P_XPOSE] - 64;
    xp_cache = xp_cache > 24 ? 24 : xp_cache < -24 ? -24 : xp_cache;

    while (evq_r != evq_w) {                    /* events from the keys and MIDI */
        uint32_t e = evq[evq_r & (EVQ - 1u)], type = e >> 14, vel = e >> 7 & 127u, note = e & 127u;
        evq_r++;
        if (type == EV_ON) user_on(note, vel);
        else if (type == EV_OFF) user_off(note);
        else if (type == EV_PANIC) {
            voice_panic();
            pmask[0] = pmask[1] = pmask[2] = pmask[3] = 0;
            lmask[0] = lmask[1] = lmask[2] = lmask[3] = 0;
            arp_cur = seq_cur = 0;
            arp_dirty = 1;
        } else if (type == EV_REST && rec_arm && !playing) {
            if (rec_fresh) { for (i = 0; i < 32u; i++) pat[i] = 0; rec_fresh = 0; }
            if (rec_cursor < 32u) pat[rec_cursor++] = 0;
        }
    }

    ra = rec_arm;
    if (ra && !prev_rec) { rec_cursor = 0; rec_fresh = !playing; }   /* arming: step record starts from a clean pattern */
    prev_rec = ra;

    ac = arp_cmd;
    if (ac != prev_arp) {
        if (!prev_arp) {                        /* the keys that sounded directly now belong to the arpeggiator */
            for (i = 0; i < 128u; i++) if (pmask[i >> 5] >> (i & 31u) & 1u) voice_off(i);
        }
        if (!ac && arp_cur) { voice_off(arp_cur); arp_cur = 0; }
        prev_arp = ac;
        arp_dirty = 1;
    }
    if (arp_dirty || octs_seen != 1u + ((uint32_t)P[P_ARPOCT] * 3u >> 7)) { arp_dirty = 0; arp_build(); }

    wp = want_play;
    if (wp && !playing) {                       /* start: the first step plays at once */
        seq_step = seq_len - 1u;
        seq_cur_len = step_len + (((seq_len - 1u) & 1u) ? 0u - swd : swd);
        seq_acc = seq_cur_len;
        playing = 1;
    } else if (!wp && playing) {
        playing = 0;
        if (seq_cur) { voice_off(seq_cur); seq_cur = 0; }
    }

    if (ac) {                                   /* the arpeggiator */
        if (!an) { arp_acc = arp_len; an_prev = 0; }
        else {
            if (!an_prev) { aidx = P[P_ARPMODE] >> 5 == 1u ? an - 1u : 0; adir = 1; arp_acc = arp_len; }
            an_prev = an;
            arp_acc += 16u;
            if (arp_acc >= arp_len) {
                arp_acc -= arp_len;
                if (arp_acc >= arp_len) arp_acc = 0;
                arp_fire();
                arp_off_at = (arp_len * gl) >> 7;
            } else if (arp_cur && arp_acc >= arp_off_at) { voice_off(arp_cur); arp_cur = 0; }
        }
    }

    if (playing) {                              /* the sequencer */
        seq_acc += 16u;
        if (seq_acc >= seq_cur_len) {
            uint32_t s, n;
            seq_acc -= seq_cur_len;
            if (seq_acc >= step_len) seq_acc = 0;
            if (seq_cur) { voice_off(seq_cur); seq_cur = 0; }
            s = seq_step + 1u >= seq_len ? 0u : seq_step + 1u;
            seq_step = s;
            seq_cur_len = step_len + ((s & 1u) ? 0u - swd : swd);
            seq_off_at = (seq_cur_len * gl) >> 7;
            n = pat[s];
            if (n & 127u) { seq_cur = clamp_note((int32_t)(n & 127u) + xp_cache); voice_on(seq_cur, n & 128u ? 127u : 90u); }
        } else if (seq_cur && seq_acc >= seq_off_at) { voice_off(seq_cur); seq_cur = 0; }
    }
}

/* ---- settings: the patch slots and the octave */
#define SET_MAGIC 0xC9u
static struct { uint8_t magic, cur; int8_t oct, pad; uint8_t slot[NPRESET][NPP], g[NP - NPP], pat[32], sp[5][4]; } set;   /* 256 B of 256 */

static uint32_t cur;                            /* current preset */
static uint32_t edited;                         /* edited since loaded */

static void load_preset(uint32_t n)
{
    uint32_t i;
    push_ev(EV_PANIC, 0, 0);
    cur = n;
    for (i = 0; i < NPP; i++) P[i] = set.slot[n][i];
    edited = 0;
}
static void set_param(uint32_t idx, int32_t val)
{
    val = val < 0 ? 0 : val > 127 ? 127 : val;
    if (P[idx] != val) { P[idx] = (uint8_t)val; if (idx < NPP) edited = 1; }   /* (the global ones are not part of a preset) */
}

/* ---- the screen: a neon HUD, the live scope, four knobs per page */
#define BG RGB(6, 4, 14)
#define C_CY RGB(0, 235, 255)
#define C_MG RGB(255, 0, 160)
#define C_YE RGB(255, 225, 0)
#define C_PU RGB(60, 38, 100)
#define C_GR RGB(24, 16, 48)
#define C_TX RGB(200, 200, 230)
#define KEY0 53u

static void itoa3(int32_t v, char *s)
{
    uint32_t i = 0, u = (uint32_t)(v < 0 ? -v : v);
    char t[4];
    uint32_t n = 0;
    if (v < 0) s[i++] = '-';
    do { t[n++] = (char)('0' + u % 10u); u /= 10u; } while (u);
    while (n) s[i++] = t[--n];
    s[i] = 0;
}
static void fmt_val(uint32_t idx, char *s)
{
    static const char *const SHP[4] = {"TRI", "SAW", "SQR", "S&H"}, *const DST[3] = {"CUTOFF", "PITCH", "PWM"};
    const char *p = 0;
    uint32_t v = P[idx];
    if (idx == P_LSHAPE) p = SHP[v >> 5];
    else if (idx == P_LDEST) p = DST[v * 3u >> 7];
    else if (idx == P_MODE) p = v >= 64u ? "MONO" : "POLY";
    else if (idx == P_ARPMODE) { static const char *const AM[4] = {"UP", "DOWN", "UP-DN", "RAND"}; p = AM[v >> 5]; }
    else if (idx == P_ARPRATE) { static const char *const AR[4] = {"1/8", "1/16", "1/16T", "1/32"}; p = AR[v >> 5]; }
    if (p) { do { *s++ = *p; } while (*p++); return; }
    if (idx == P_ARPOCT) v = 1u + (v * 3u >> 7);
    else if (idx == P_GATE) v = (16u + (v * 112u >> 7)) * 100u >> 7;
    else if (idx == P_BPM) v = 60u + v;
    else if (idx == P_SWING) v = 50u + v * 25u / 127u;
    else if (idx == P_LEN) v = 1u + (v >> 2);
    if (idx == P_XPOSE) { int32_t x = (int32_t)v - 64; itoa3(x > 24 ? 24 : x < -24 ? -24 : x, s); return; }
    itoa3(idx == P_ENVAMT || idx == P_TUNE ? (int32_t)v - 64 : (int32_t)v, s);
}

static void draw_title(void)
{
    char s[24];
    uint32_t n = 0;
    const char *p = PNAME[cur];
    cv_begin(240, 56, BG);
    cv_text(10, 0, &FONT_L, "TRIPDECK", C_MG);      /* chromatic aberration: magenta, cyan, then white on top */
    cv_text(6, 0, &FONT_L, "TRIPDECK", C_CY);
    cv_text(8, 0, &FONT_L, "TRIPDECK", C_WHITE);
    s[n++] = (char)('1' + cur); s[n++] = ' ';
    while (*p && n < 20u) s[n++] = *p++;
    if (edited) s[n++] = '*';
    s[n] = 0;
    cv_rect(0, 36, 240, 1, C_PU);
    cv_text(8, 38, &FONT_S, s, C_YE);
    {                                           /* right side: tempo, arp, transport, record */
        char bpm[8];
        const char *arp = arp_cmd == 2u ? "HLD" : "ARP";
        int32_t x;
        itoa3(60 + P[P_BPM], bpm);
        x = 236 - text_w(&FONT_S, bpm) - (arp_cmd ? text_w(&FONT_S, arp) + 8 : 0) - (playing || want_play ? 17 : 0)
            - (rec_arm ? text_w(&FONT_S, "REC") + 8 : 0);
        x = cv_text(x, 38, &FONT_S, bpm, C_TX) + 8;
        if (arp_cmd) x = cv_text(x, 38, &FONT_S, arp, C_CY) + 8;
        if (playing || want_play) x = cv_text(x, 38, &FONT_S, ">", C_CY) + 8;
        if (rec_arm) cv_text(x, 38, &FONT_S, "REC", C_MG);
    }
    cv_blit(0, 0);
}

static void draw_seq(void)                      /* the pattern as a row of steps: bar height = pitch, yellow = accent */
{
    uint32_t s, len = 1u + (P[P_LEN] >> 2), mn = 255, mx = 0, curs = playing ? seq_step : 255u, rc = rec_arm && !playing ? rec_cursor : 255u;
    for (s = 0; s < 32u; s++) {
        uint32_t n = pat[s] & 127u;
        if (n) { if (n < mn) mn = n; if (n > mx) mx = n; }
    }
    cv_begin(240, 64, BG);
    for (s = 0; s < len && s < 32u; s++) {
        int32_t x = (int32_t)(s & 15u) * 15 + 1, y = (int32_t)(s >> 4) * 32 + 2;
        uint32_t n = pat[s] & 127u;
        uint16_t edge = s == curs ? C_YE : s == rc ? C_MG : (s & 3u) == 0 ? C_PU : C_GR;
        cv_rect(x, y, 13, 1, edge); cv_rect(x, y + 27, 13, 1, edge);
        cv_rect(x, y, 1, 28, edge); cv_rect(x + 12, y, 1, 28, edge);
        if (n) {
            int32_t h = mx > mn ? 4 + (int32_t)((n - mn) * 20u / (mx - mn)) : 14;
            cv_rect(x + 2, y + 26 - h, 9, h, pat[s] & 0x80u ? C_YE : s == curs ? C_WHITE : C_CY);
        } else if (s == rc)
            cv_rect(x + 3, y + 12, 7, 3, C_MG);
    }
    cv_blit(0, 56);
}

static void draw_scope(void)
{
    uint32_t end = scope_w, a = end - 1500u, i, start = a;
    int32_t x, py = 32, y;
    cv_begin(240, 64, BG);
    for (x = 0; x < 240; x += 30) cv_rect(x, 0, 1, 64, C_GR);
    cv_rect(0, 32, 240, 1, C_GR);
    cv_rect(0, 0, 14, 1, C_MG); cv_rect(0, 0, 1, 8, C_MG); cv_rect(226, 0, 14, 1, C_MG); cv_rect(239, 0, 1, 8, C_MG);
    cv_rect(0, 63, 14, 1, C_MG); cv_rect(0, 56, 1, 8, C_MG); cv_rect(226, 63, 14, 1, C_MG); cv_rect(239, 56, 1, 8, C_MG);
    for (i = 0; i < 600u; i++)                  /* trigger on a rising zero crossing */
        if (scope[(a + i) & (SCOPE_N - 1u)] < 0 && scope[(a + i + 1u) & (SCOPE_N - 1u)] >= 0) { start = a + i; break; }
    for (x = 0; x < 240; x++) {
        y = 32 - ((int32_t)scope[(start + (uint32_t)x * 3u) & (SCOPE_N - 1u)] * 28 >> 15);
        if (x) { cv_line(x - 1, py + 1, x, y + 1, C_MG); cv_line(x - 1, py, x, y, C_CY); }
        py = y;
    }
    cv_blit(0, 56);
}

static void draw_row(uint32_t page, uint32_t r, uint32_t sel)
{
    char s[12];
    uint32_t idx = page * 4u + r, v = P[idx];
    cv_begin(240, 20, BG);
    cv_text(8, 2, &FONT_S, LABEL[idx], r == sel ? C_YE : C_CY);
    cv_rect(112, 6, 90, 9, C_PU);
    cv_rect(113, 7, 88, 7, BG);
    cv_rect(113, 7, (int32_t)(v * 88u / 127u), 7, r == sel ? C_YE : C_MG);
    fmt_val(idx, s);
    cv_text(236 - text_w(&FONT_S, s), 2, &FONT_S, s, C_WHITE);
    cv_blit(0, 122 + r * 20u);
}

static void draw_tabs(uint32_t page)
{
    uint32_t t;
    cv_begin(240, 16, BG);
    for (t = 0; t < 8u; t++) {
        if (t == page) cv_rect((int32_t)t * 30, 0, 29, 16, C_CY);
        cv_text((int32_t)t * 30 + 15 - text_w(&FONT_S, TAB[t]) / 2, 0, &FONT_S, TAB[t], t == page ? BG : C_PU);
    }
    cv_blit(0, 206);
}

static void draw_status(int32_t oct, const char *msg)
{
    char s[40], n[8];
    uint32_t i = 0, j;
    const char *p = "CPU ";
    while (*p) s[i++] = *p++;
    itoa3((int32_t)cpu_pct, n);
    for (j = 0; n[j]; j++) s[i++] = n[j];
    s[i++] = '%'; s[i++] = ' '; s[i++] = ' ';
    p = is_mono() ? "MONO" : "POLY";
    while (*p) s[i++] = *p++;
    s[i++] = ' '; s[i++] = ' '; s[i++] = 'O'; s[i++] = 'C'; s[i++] = 'T'; s[i++] = oct < 0 ? '-' : '+'; s[i++] = (char)('0' + (oct < 0 ? -oct : oct));
    if (msg) { s[i++] = ' '; s[i++] = ' '; while (*msg) s[i++] = *msg++; }
    s[i] = 0;
    draw_text_box(0, 224, 240, &FONT_S, s, C_PU, 1);
}

static const uint8_t CC_MAP[6][2] = {{74, P_CUT}, {71, P_RES}, {73, P_ATK}, {72, P_REL}, {1, P_LDEPTH}, {91, P_DMIX}};
/* MUTATE (HOME): a random walk of the patch. Each knob that may move has limits that keep the patch playable:
 * no eight-second attacks, no runaway feedback, the filter never fully shut. MODE, TUNE and the globals are left alone. */
static const struct { uint8_t idx, lo, hi; } MUT[] = {
    {P_MIX, 0, 127}, {P_PW, 10, 118}, {P_DET, 0, 100}, {P_SUB, 0, 127},
    {P_CUT, 35, 127}, {P_RES, 0, 105}, {P_ENVAMT, 0, 127}, {P_DRV, 0, 110},
    {P_ATK, 0, 90}, {P_DEC, 20, 127}, {P_SUS, 0, 127}, {P_REL, 10, 110},
    {P_LRATE, 0, 127}, {P_LDEPTH, 0, 110}, {P_LDEST, 0, 127}, {P_LSHAPE, 0, 127},
    {P_CRUSH, 0, 100}, {P_DMIX, 0, 90}, {P_DTIME, 0, 127}, {P_DFB, 0, 100},
    {P_GLIDE, 0, 80}, {P_VELO, 0, 127}};
#define NMUT (sizeof MUT / sizeof MUT[0])
static uint32_t mrnd = 2463534242u;
static uint32_t mrand(void) { mrnd ^= mrnd << 13; mrnd ^= mrnd >> 17; mrnd ^= mrnd << 5; return mrnd; }

static void mutate(void)
{
    uint32_t n = 4u + mrand() % 3u, i;
    mrnd ^= fm1_ms * 2654435761u;               /* every press is different */
    for (i = 0; i < n; i++) {
        uint32_t m = mrand() % NMUT, idx = MUT[m].idx;
        int32_t v = idx == P_LDEST || idx == P_LSHAPE ? (int32_t)(mrand() & 127u)       /* a jump: pick a new target or shape */
                                                      : (int32_t)P[idx] + (int32_t)(mrand() % 41u) - 20;   /* a step of +-20 */
        v = v < MUT[m].lo ? MUT[m].lo : v > MUT[m].hi ? MUT[m].hi : v;
        set_param(idx, v);
    }
}

static const uint8_t PAT0[32] = {0xAD, 0, 45, 0, 48, 0, 45, 0, 43, 0, 43, 45, 0, 50, 0, 48};   /* a default line in A minor */

#include "scene_vis.h"

static const uint8_t SP0[5][4] = {{50, 70, 50, 40}, {60, 40, 60, 30}, {50, 70, 50, 20}, {100, 100, 40, 50}, {10, 70, 70, 60}};

static void app_main(void)
{
    static const uint8_t SCENE_BTN[5] = {BTN_EDIT, BTN_ENV, BTN_LFO, BTN_FX, BTN_GLO};
    uint32_t prev = 0, k, t_scope = 0, t_stat = 0, t_change = 0, redraw = 1, page = 0, sel = 0, sel_drawn = 0;
    uint32_t pending_save = 0, was_mono, lit_drawn = ~0u, edited_drawn = 0, sig_drawn = ~0u, home_t = 0, home_long = 0, scene_drawn = 0;
    uint8_t shown[NP];
    int32_t oct_drawn = 99;
    uint8_t held[27] = {0};
    const char *msg = 0;
    uint32_t t_msg = 0;

    settings_load(&set, sizeof set);
    if (set.magic != SET_MAGIC || set.cur >= NPRESET || set.oct < -2 || set.oct > 2) {
        uint32_t i, j;
        set.magic = SET_MAGIC; set.cur = 0; set.oct = 0; set.pad = 0;
        for (i = 0; i < NPRESET; i++) for (j = 0; j < NPP; j++) set.slot[i][j] = FACTORY[i][j];
        for (i = 0; i < NP - NPP; i++) set.g[i] = GLOBAL0[i];
        for (i = 0; i < 32u; i++) set.pat[i] = PAT0[i];
        for (i = 0; i < 5u; i++) for (j = 0; j < 4u; j++) set.sp[i][j] = SP0[i][j];
    }
    for (k = 0; k < 20u; k++) SP[k >> 2][k & 3u] = set.sp[k >> 2][k & 3u];
    vis_init();
    for (k = 0; k < NP - NPP; k++) P[NPP + k] = set.g[k];
    for (k = 0; k < 32u; k++) pat[k] = set.pat[k];
    load_preset(set.cur);
    was_mono = (uint32_t)is_mono();
    for (k = 0; k < NP; k++) shown[k] = 255;

    lcd_fill(0, 0, 240, 240, BG);

    for (;;) {
        uint32_t keys, down, up, rel, b, lit = 0, base = (uint32_t)(KEY0 + 12 * set.oct), r, sig;
        uint8_t st, d1, d2;
        int32_t d;
        platform_poll();

        keys = fm1_in.notes;
        down = keys & ~prev;
        up = prev & ~keys;
        prev = keys;
        for (k = 0; k < 27u; k++) {
            if (down >> k & 1u) { held[k] = (uint8_t)(base + k); push_ev(EV_ON, held[k], 100); midi_send(0x90, held[k], 100); }
            if (up >> k & 1u) { push_ev(EV_OFF, held[k], 0); midi_send(0x80, held[k], 0); }
        }

        b = fm1_input_edges(&rel);
        for (k = 0; k < 5u; k++)                /* a scene button: that scene; again: out of the scene */
            if (b & (1u << SCENE_BTN[k])) { scene_cur = (uint8_t)(scene_cur == k + 1u ? 0u : k + 1u); vstrip = 0; vtouch = 0; }
        if (b & (1u << BTN_SEL)) {              /* the synth pages, one per press */
            page = (scene_cur || page >= 5u) ? 0u : page + 1u;
            scene_cur = 0; redraw = 1;
        }
        if (b & (1u << BTN_ARP)) {              /* ARP: off -> on (and its page) -> hold -> off; in a scene it only cycles */
            if (!scene_cur && page != 6u) { page = 6; redraw = 1; if (!arp_cmd) arp_cmd = 1; }
            else arp_cmd = (uint8_t)(arp_cmd == 0u ? 1u : arp_cmd == 1u ? 2u : 0u);
        }
        if (b & (1u << BTN_SEQ)) { page = 7; scene_cur = 0; redraw = 1; }
        if (b & (1u << BTN_PLAY)) want_play = (uint8_t)!want_play;
        if (b & (1u << BTN_REC)) {              /* arm: the next notes are recorded; disarm: a step recording sets the length */
            if (rec_arm) {
                rec_arm = 0;
                if (!playing && rec_cursor > 0u) set_param(P_LEN, (int32_t)(rec_cursor - 1u) * 4 + 1);
                msg = "REC OFF"; t_msg = fm1_ms;
            } else { rec_arm = 1; msg = "REC"; t_msg = fm1_ms; }
        }
        if (b & (1u << BTN_HOME)) { home_t = fm1_ms; home_long = 0; }
        if (home_t && !home_long && (fm1_in.buttons >> BTN_HOME & 1u) && fm1_ms - home_t > 600u) {   /* held: panic */
            home_long = 1;
            push_ev(EV_PANIC, 0, 0);
            msg = "PANIC"; t_msg = fm1_ms;
        }
        if (rel & (1u << BTN_HOME)) {           /* tapped: a rest while step recording, else mutate the patch */
            if (home_t && !home_long) {
                if (rec_arm && !playing) push_ev(EV_REST, 0, 0);
                else if (scene_cur) {           /* a surprise: four new knob positions */
                    for (k = 0; k < 4u; k++) SP[scene_cur - 1u][k] = (uint8_t)(mrand() & 127u);
                    msg = "SURPRISE"; t_msg = fm1_ms;
                } else { mutate(); msg = "MUTATED"; t_msg = fm1_ms; }
            }
            home_t = 0;
        }
        if (b & (1u << BTN_OCT_DOWN) && set.oct > -2) { set.oct--; pending_save = 1; t_change = fm1_ms; }
        if (b & (1u << BTN_OCT_UP) && set.oct < 2) { set.oct++; pending_save = 1; t_change = fm1_ms; }
        if (b & (1u << BTN_SAVE)) {             /* the preset slot, the global settings and the pattern */
            for (k = 0; k < NPP; k++) set.slot[cur][k] = P[k];
            for (k = 0; k < NP - NPP; k++) set.g[k] = P[NPP + k];
            for (k = 0; k < 32u; k++) set.pat[k] = pat[k];
            for (k = 0; k < 20u; k++) set.sp[k >> 2][k & 3u] = SP[k >> 2][k & 3u];
            set.cur = (uint8_t)cur; edited = 0; pending_save = 1; t_change = fm1_ms; redraw = 1;
            msg = "SAVED"; t_msg = fm1_ms;
        }

        for (k = 0; k < 4u; k++) {              /* the four knobs of the page */
            d = fm1_enc_take(ENC_KNOB1 + k);
            if (d && scene_cur) {               /* the scene's knobs: the picture and the sound change together */
                int32_t v = (int32_t)SP[scene_cur - 1u][k] + d * (d > 2 || d < -2 ? 6 : 2);
                SP[scene_cur - 1u][k] = (uint8_t)(v < 0 ? 0 : v > 127 ? 127 : v);
                vtouch_ms[k] = fm1_ms; vtouch = k + 1u;
            } else if (d) { set_param(page * 4u + k, (int32_t)P[page * 4u + k] + d * (d > 2 || d < -2 ? 6 : 2)); sel = k; }
        }
        d = fm1_enc_take(ENC_ALGORITHM);
        if (d) { set_param(P_CUT, (int32_t)P[P_CUT] + d * (d > 2 || d < -2 ? 6 : 2)); if (scene_cur) { msg = "CUTOFF"; t_msg = fm1_ms; } }
        d = fm1_enc_take(ENC_SELECT);
        if (d) { set_param(P_RES, (int32_t)P[P_RES] + d * (d > 2 || d < -2 ? 6 : 2)); if (scene_cur) { msg = "RESONANCE"; t_msg = fm1_ms; } }
        d = fm1_enc_take(ENC_PRESETS);
        if (d) {
            int32_t n = (int32_t)cur + (d > 0 ? 1 : -1);
            load_preset((uint32_t)(n < 0 ? (int32_t)NPRESET - 1 : n >= (int32_t)NPRESET ? 0 : n));
            redraw = 1;
            if (scene_cur) { msg = PNAME[cur]; t_msg = fm1_ms; }
        }
        if ((uint32_t)is_mono() != was_mono) { was_mono = (uint32_t)is_mono(); push_ev(EV_PANIC, 0, 0); }

        while (midi_get(&st, &d1, &d2)) {
            if ((st & 0xF0u) == 0x90u && d2) push_ev(EV_ON, d1, d2);
            else if ((st & 0xF0u) == 0x80u || (st & 0xF0u) == 0x90u) push_ev(EV_OFF, d1, 0);
            else if ((st & 0xF0u) == 0xB0u)
                for (k = 0; k < 6u; k++)
                    if (CC_MAP[k][0] == d1) set_param(CC_MAP[k][1], d2);
        }

        base = (uint32_t)(KEY0 + 12 * set.oct);
        for (k = 0; k < NV; k++)
            if (voice[k].gate && voice[k].note >= base && voice[k].note < base + 27u)
                lit |= 1u << (voice[k].note - base);
        if (lit != lit_drawn) {
            lit_drawn = lit;
            for (k = 0; k < 27u; k++) fm1_led_key(KEY_LED(k), lit >> k & 1u);
        }
        for (k = 0; k < 5u; k++) fm1_led_key(SCENE_BTN[k], scene_cur == k + 1u);
        fm1_led_key(BTN_SEL, !scene_cur && page < 6u);
        fm1_led_key(BTN_ARP, arp_cmd != 0u);
        fm1_led_key(BTN_SEQ, !scene_cur && page == 7u);
        fm1_led_key(BTN_PLAY, playing && (seq_step & 3u) == 0u);   /* blinks on the beat */
        fm1_led_key(BTN_REC, rec_arm);
        fm1_led_key(BTN_OCT_DOWN, set.oct < 0);
        fm1_led_key(BTN_OCT_UP, set.oct > 0);

        vnote = 0;                              /* a held key, for the pictures */
        for (k = 0; k < NV; k++) if (voice[k].gate) vnote = voice[k].note;
        if (msg && fm1_ms - t_msg > 1500u) { msg = 0; oct_drawn = 99; }
        vmsg = msg;
        if (vtouch && fm1_ms - vtouch_ms[vtouch - 1u] > 1500u) vtouch = 0;
        if (scene_cur) {                        /* a scene: one strip of the picture per pass */
#ifdef DINGHY_HOST
            static uint32_t t_strip;
            if (fm1_ms - t_strip >= 8u) { t_strip = fm1_ms; vis_step(scene_cur); }     /* (about what the screen link manages) */
#else
            vis_step(scene_cur);
#endif
            scene_drawn = scene_cur;
            if (pending_save && quiet() && !playing && fm1_ms - t_change > 1000u) { settings_save(&set, sizeof set); pending_save = 0; }
            continue;
        }
        if (scene_drawn) {                      /* back from a scene: the synth screen again */
            scene_drawn = 0; lcd_fill(0, 0, 240, 240, BG); redraw = 1; oct_drawn = 99; t_stat = fm1_ms - 1000u;
        }
        /* the screen: only what changed */
        sig = arp_cmd | (playing ? 4u : 0u) | (rec_arm ? 8u : 0u) | (edited ? 16u : 0u) | (uint32_t)P[P_BPM] << 8;
        if (redraw) { draw_title(); draw_tabs(page); for (k = 0; k < NP; k++) shown[k] = 255; redraw = 0; oct_drawn = 99; sig_drawn = sig; edited_drawn = edited; }
        for (r = 0; r < 4u; r++)
            if (shown[page * 4u + r] != P[page * 4u + r]) {
                shown[page * 4u + r] = P[page * 4u + r];
                draw_row(page, r, sel);
            }
        if (sel != sel_drawn) {                 /* the highlight follows the last turned knob */
            sel_drawn = sel;
            for (r = 0; r < 4u; r++) shown[page * 4u + r] = 255;
        }
        if (sig != sig_drawn || edited != edited_drawn) { sig_drawn = sig; edited_drawn = edited; draw_title(); }
        if (fm1_ms - t_scope >= 50u) { t_scope = fm1_ms; if (page == 7u) draw_seq(); else draw_scope(); }
        if (msg && fm1_ms - t_msg > 1500u) { msg = 0; oct_drawn = 99; }
        if (set.oct != oct_drawn || fm1_ms - t_stat >= 500u) {
            oct_drawn = set.oct; t_stat = fm1_ms;
            draw_status(set.oct, msg);
        }

        if (pending_save && quiet() && !playing && fm1_ms - t_change > 1000u) {   /* an erase stalls the sound: only when silent */
            settings_save(&set, sizeof set);
            pending_save = 0;
        }
    }
}
