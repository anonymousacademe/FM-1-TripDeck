/* SPDX-License-Identifier: GPL-3.0-only */
/* TRIPDECK: a psychedelic synth for the M-VAVE FM-1. Four voices (PAD, SITAR with a tamboura drone, ORGAN through a Leslie, FM BELL),
 * a vowel filter, a spiralling echo, a shimmering and freezable reverb, scale lock, and a TRIP mode that lets the patch drift.
 *
 * Ten pages of four knobs: OSC FLT ENV LFO DLY REV VOX GLO ARP SEQ. Every page is a full-screen psychedelic picture, and its four
 * knobs change the sound AND the picture. HOME hides the picture to show the parameters (and back).
 *
 *   EDIT OSC/FLT   ENV   LFO   FX DLY/REV/VOX   GLO   ARP   SEQ   buttons jump to a page (EDIT and FX step through theirs)
 *   SEL            the next page
 *   HOME           tap: picture <-> parameters   double-tap: mutate the patch   hold: panic
 *   PLAY REC OCT- OCT+ SAVE   as in CyberDeck;   ALGORITHM / SELECT / PRESETS   cutoff / resonance / patch, on every page
 *
 * The arpeggiator, sequencer and patch mutator come from CyberDeck. Built on CyberDeck and Dinghy (zvenson); the platform is
 * Felucca (Leo Kuroshita, Hugelton Instruments) and SLOOP (isod89).
 * Integers only (no float, no 64-bit division). Pictures: scene_vis.h (main loop). Voices and reverb: voices.h, fx.h (audio ISR). */
#include "platform.c"
#include "tables.h"

#ifndef FM1_TICKS_PER_US
#define FM1_TICKS_PER_US 24u
#endif

/* ---- parameters: 10 pages x 4 knobs, each 0..127 */
enum { P_VOICE, P_O1, P_O2, P_O3,               /* OSC   the voice; then three knobs that change meaning with it */
       P_CUT, P_RES, P_ENVAMT, P_DRV,           /* FLT */
       P_ATK, P_DEC, P_SUS, P_REL,              /* ENV */
       P_LRATE, P_LDEPTH, P_LDEST, P_LSHAPE,    /* LFO */
       P_DMIX, P_DTIME, P_DFB, P_SPIRAL,        /* DLY   the echo; SPIRAL shifts the pitch of every repeat */
       P_RVB, P_SHIM, P_DAMP, P_FREEZE,         /* REV */
       P_VOWEL, P_TALK, P_SHIFT, P_SWEEP,       /* VOX   ---- the 28 above belong to the preset; the rest are global ---- */
       P_GLIDE, P_TRIP, P_SCALE, P_ROOT,        /* GLO   GLIDE 0 = poly, else mono */
       P_ARPMODE, P_ARPOCT, P_ARPRATE, P_GATE,  /* ARP */
       P_BPM, P_SWING, P_LEN, P_XPOSE,          /* SEQ */
       NP };
#define NPP 28u                                 /* params per preset */
static volatile uint8_t P[NP];                  /* what the knobs set */
static volatile uint8_t PE[NP];                 /* what the sound and the pictures use: P plus the drift of TRIP */
static int8_t PD[NP];

#define NPRESET 8u
static const uint8_t GLOBAL0[NP - NPP] = {0, 0, 0, 0,   0, 20, 40, 80,   60, 0, 60, 64};   /* poly, no trip, no scale | up, 1 oct, 1/16, gate 67 % | 120 bpm, no swing, 16 steps, +0 */
static const uint8_t FACTORY[NPRESET][NPP] = {
    /* VOICE: 0-31 PAD, 32-63 SITAR, 64-95 ORGAN, 96-127 BELL. The three OSC knobs after it change meaning with the voice (see voices.h). */
    /*        VOI O1  O2  O3 | CUT RES ENV DRV | ATK DEC SUS REL | LRT LDP LDS LSH | DMX DTM DFB SPI | RVB SHM DMP FRZ | VOW TLK SFT SWP */
    /* SKY PAD      */ { 16, 70, 45, 20,   55, 20, 84, 10,   70, 90,100, 95,   15, 22, 50,  0,   35, 80, 60, 78,   70, 50, 70,  0,   40,  0, 64,  0},
    /* CHOIR        */ { 16, 64, 35,  0,   70, 10, 70,  0,   60, 90,110, 85,   55, 14, 50,  0,   25, 70, 45, 64,   80, 70, 90,  0,   20,100, 64, 25},
    /* SUNSHINE     */ { 48, 70, 55, 70,   90,  0, 64,  0,    0, 80,127, 45,    0,  0, 10,  0,   30, 90, 50, 64,   45, 30, 80,  0,   40,  0, 64,  0},
    /* RAGA DRONE   */ { 48,100, 90,100,   60,  0, 64,  0,    0,100,127, 60,    0,  0, 10,  0,   35, 90, 55, 70,   60, 40, 60,  0,   40,  0, 64,  0},
    /* PURPLE ORGAN */ { 80, 80, 50, 40,   64,  0, 64,  0,    0, 20,127,  8,    0,  0, 10,  0,    0, 60, 20, 64,   30,  0, 90,  0,   40,  0, 64,  0},
    /* GLASS BELLS  */ {112, 48, 55, 40,   64,  0, 64,  0,    0, 70, 30, 70,    0,  0, 10,  0,   40,100, 60, 82,   85, 90, 60,  0,   40,  0, 64,  0},
    /* DREAM KEYS   */ {112,  8, 40, 50,   64,  0, 64,  0,    0, 75, 20, 50,    0,  0, 10,  0,   25, 80, 45, 64,   50, 20, 70,  0,   40,  0, 64,  0},
    /* SPIRAL DUB   */ { 16, 30, 70, 40,   45, 40, 90, 25,    0, 50, 60, 70,   30, 40, 10,  0,   50, 70, 85, 40,   55,  0,100,  0,   40,  0, 64,  0},
};
static const char *const PNAME[NPRESET] = {"SKY PAD", "CHOIR", "SUNSHINE", "RAGA DRONE", "PURPLE ORGAN", "GLASS BELLS", "DREAM KEYS", "SPIRAL DUB"};
static const char *const LABEL[NP] = {
    "VOICE", "PULSE W", "DETUNE", "SUB",
    "CUTOFF", "RESO", "ENV AMT", "DRIVE",
    "ATTACK", "DECAY", "SUSTAIN", "RELEASE",
    "RATE", "DEPTH", "DEST", "SHAPE",
    "DLY MIX", "DLY TIME", "FEEDBACK", "SPIRAL",
    "REVERB", "SHIMMER", "DAMPING", "FREEZE",
    "VOWEL", "TALK", "SHIFT", "SWEEP",
    "GLIDE", "TRIP", "SCALE", "ROOT",
    "ARP MODE", "OCTAVES", "RATE", "GATE",
    "BPM", "SWING", "LENGTH", "TRANSPOSE"};

#define NPAGE 10u
static const char *const TAB[NPAGE] = {"OSC", "FLT", "ENV", "LFO", "DLY", "REV", "VOX", "GLO", "ARP", "SEQ"};

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
    uint32_t kw, kinj;                          /* sitar: the string's write position, noise still to inject */
    int32_t klp, kscoop, klast;                 /* sitar: loop filter, the scoop (meend), the last sample */
} voice_t;
static voice_t voice[NV];

#define SCOPE_N 2048u
static int16_t scope[SCOPE_N];
static volatile uint32_t scope_w, cpu_pct;
static volatile int32_t bend;                   /* MIDI pitch bend, -8192..8191, about +-2 semitones (main writes, ISR reads) */

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
#include "fx.h"
#include "voices.h"
static void engine_tick(void);

static void app_audio(int32_t *out, uint32_t n)
{
    static uint32_t lph, rnd = 0x1234567u, dw;
    static int32_t lfo, sh, dt_cur = 20000, lpL, lpR, pk, lgain = 32768;
    static uint32_t cpu_max, cpu_blk;
    uint32_t t0 = fm1_ticks(), b, v, i;

    for (b = 0; b < n; b += 16u) {
        int32_t mono[16], m, dtt, edmix = PE[P_DMIX], edfb = (int32_t)((PE[P_DFB] * 113u) >> 7), edt = 1500 + (int32_t)PE[P_DTIME] * 245;
        uint32_t prev_lph = lph, shape = PE[P_LSHAPE] >> 5, dest = (uint32_t)PE[P_LDEST] * 3u >> 7;
        uint32_t mono_mode = PE[P_GLIDE] != 0u, eng = ENG(), les_on = eng == 2u && PE[P_O2] != 0u, sp_on;
        int32_t ldep = PE[P_LDEPTH];

        /* LFO, once per tick */
        lph += LFO_INC[PE[P_LRATE]];
        if (lph < prev_lph) { rnd ^= rnd << 13; rnd ^= rnd >> 17; rnd ^= rnd << 5; sh = (int32_t)(rnd >> 16) - 32768; }
        {
            int32_t s = (int32_t)lph >> 16;
            lfo = shape == 0u ? ((s < 0 ? -s : s) << 1) - 32767 : shape == 1u ? s : shape == 2u ? (s < 0 ? -32767 : 32767) : sh;
            lfo = (lfo * ldep) >> 7;            /* depth applied: +-32512 */
        }

        engine_tick();                          /* events, arpeggiator, sequencer: they start and stop voices */
        kick_tick();
        wow_tick();
        drone_tick(eng == 1u && PE[P_O3] != 0u);
        leslie_tick(les_on ? PE[P_O2] : 0u);
        vowel_tick();
        reverb_tick();
        spiral_tick();
        sp_on = sp_rate != 0;
        for (i = 0; i < 16u; i++) mono[i] = 0;

        for (v = 0; v < NV; v++) {
            voice_t *V = &voice[v];
            uint32_t st = V->stage, inc, incb, pa, pb, ps, wph, a1, a2, a3, g, k, gk, den;
            int32_t env, ei, de, fenv, ic1, ic2, velg, ci, pwq, dg, sublvl, mixa, mixb;
            uint32_t gate = V->gate, fresh = 0;

            if (V->trig != V->seen) {           /* a new note */
                V->seen = V->trig;
                if (st == ST_OFF) { V->pa = V->pb = V->ps = 0; V->ic1 = V->ic2 = 0; }
                st = ST_ATK;
                V->fenv = ONE;
                fresh = 1;
                if (eng == 1u && !dr_t) drone_note(V->note);
                kick = 32767;                   /* the picture and the voices strike with the note */
                if (!mono_mode) V->inc = V->inc_t;
            }
            if (st == ST_OFF) continue;
            if (!gate && (st == ST_ATK || st == ST_DEC)) st = ST_REL;
            if (gate && eng == 1u) dr_t = DRT;

            /* control rate: glide, envelopes */
            inc = V->inc;
            if (mono_mode)
                inc = (uint32_t)((int32_t)inc + (int32_t)(((int64_t)((int32_t)(V->inc_t - inc)) * GLIDE_R[PE[P_GLIDE]]) >> 16));
            else
                inc = V->inc_t;
            V->inc = inc;

            env = V->env;
            {
                int32_t pe = env, sus = (int32_t)PE[P_SUS] * 66052;
                if (st == ST_ATK) {
                    env += (int32_t)(((int64_t)(ONE + ONE / 3 - env) * RATE16[PE[P_ATK]]) >> 24);
                    if (env >= ONE) { env = ONE; st = ST_DEC; }
                } else if (st == ST_DEC) {
                    env += (int32_t)(((int64_t)(sus - env) * RATE16[PE[P_DEC]]) >> 24);
                    if (sus < (1 << 12) && env < (1 << 12)) st = ST_OFF;
                } else {
                    int32_t d = (int32_t)(((int64_t)env * RATE16[PE[P_REL]]) >> 24);
                    env -= d < 1 ? 1 : d;
                    if (env < (1 << 12)) { env = 0; st = ST_OFF; }
                }
                ei = pe;
                de = (env - pe) >> 4;
            }
            V->env = env;
            V->stage = (uint8_t)st;
            fenv = V->fenv;
            fenv -= (int32_t)(((int64_t)fenv * RATE16[PE[P_DEC]]) >> 24);
            V->fenv = fenv;

            /* oscillator steps with the LFO */
            if (dest == 1u) inc += (int32_t)(((int64_t)inc * lfo) >> 18);
            inc += (uint32_t)(((int64_t)(int32_t)inc * bend) >> 16);          /* the pitch wheel, for every engine: 8192 / 65536 = +12.5 % at the top */
            if (eng == 0u) inc += (int32_t)(((int64_t)inc * wow) >> 20);     /* the pad drifts like tape */
            else if (eng == 1u) { voice_sitar(V, v, inc, st, fresh, ei, de, mono); continue; }
            else if (eng == 2u) { voice_organ(V, inc, ei, de, mono); continue; }
            else { voice_bell(V, inc, ei, de, mono); continue; }
            incb = inc + (uint32_t)(((uint64_t)inc * PE[P_O2] * 26u) >> 16);
            pwq = ((int32_t)PE[P_O1] - 64) * 3;
            if (dest == 2u) pwq += lfo >> 8;
            pwq = pwq > 240 ? 240 : pwq < -240 ? -240 : pwq;
            wph = 0x80000000u + (uint32_t)(pwq * (1 << 23));
            mixb = 32; mixa = 31;               /* the pad: saw and pulse in equal parts (and as loud as the other voices) */
            sublvl = PE[P_O3];

            /* filter coefficients (Simper SVF) */
            ci = (int32_t)PE[P_CUT] * 2 + (int32_t)(((int64_t)(fenv >> 8) * ((int32_t)PE[P_ENVAMT] - 64)) >> 14) + ((int32_t)V->note - 60);
            if (dest == 0u) ci += (lfo * 96) >> 15;
            ci = ci < 0 ? 0 : ci > 255 ? 255 : ci;
            g = CUT_G[ci];
            k = 131072u - (uint32_t)PE[P_RES] * 980u;
            gk = (uint32_t)(((uint64_t)g * (g + k)) >> 16);
            den = 65536u + gk;
            a1 = 0xFFFFFFFFu / den;
            if (a1 > 65535u) a1 = 65535u;
            a2 = (uint32_t)(((uint64_t)g * a1) >> 16);
            a3 = (uint32_t)(((uint64_t)g * a2) >> 16);
            dg = 256 + (int32_t)PE[P_DRV] * 16;
            velg = 32767 - ((127 - (int32_t)V->vel) * 100);

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

        /* ping-pong echo (with its spiral), the Leslie, the reverb, out */
        dtt = edt;
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
            int32_t dry, dryL, dryR, yL, yR, fL, fR, L, R, wd;
            uint32_t rd;
            m = (mono[i] >> 1) + drone_sample();
            if (vw_talk) m = vowel_frame(m);    /* the vowel filter, before the space */
            dry = m;
            rd = (dw - (uint32_t)dt_cur) & (DLEN - 1u);
            yL = dlL[rd]; yR = dlR[rd];
            lpL += (yL - lpL) >> 2; lpR += (yR - lpR) >> 2;     /* every repeat is darker */
            fL = (lpL * edfb) >> 7;
            fR = (lpR * edfb) >> 7;
            if (sp_on) { fL = ps_run(&spL, fL, sp_rate); fR = ps_run(&spR, fR, sp_rate); }   /* ... and shifted again: the spiral */
            wd = dry + fR;                      /* ping-pong: input and the right repeat feed the left, left feeds right */
            dlL[dw] = (int16_t)(wd > 32767 ? 32767 : wd < -32767 ? -32767 : wd);
            dlR[dw] = (int16_t)(fL > 32767 ? 32767 : fL < -32767 ? -32767 : fL);
            dw = (dw + 1u) & (DLEN - 1u);
            if (les_on) leslie_frame(m, &dryL, &dryR); else { dryL = m; dryR = m; }
            L = dryL + ((yL * edmix) >> 7);
            R = dryR + ((yR * edmix) >> 7);
            reverb_frame(&L, &R);
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
    vis_lfo = lfo;
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

static uint32_t note_inc(uint32_t note) { return INC_C9[note % 12u] >> (10u - note / 12u); }
static int is_mono(void) { return PE[P_GLIDE] != 0u; }

/* MIDI out over USB: what the arpeggiator and the sequencer play (the keys you press are sent from the main loop).
 * The audio ISR is the only writer of this ring and the main loop the only reader and the only caller of midi_send. */
#define MOQ 64u
static volatile uint32_t moq[MOQ], moq_w, moq_r;
static void mout_push(uint32_t st, uint32_t d1, uint32_t d2)
{
    uint32_t w = moq_w;
    if (w - moq_r >= MOQ) return;
    moq[w & (MOQ - 1u)] = st | (d1 & 127u) << 8 | (d2 & 127u) << 16;
    moq_w = w + 1u;
}
enum { SRC_KEY, SRC_AUTO };                     /* who starts a note: a key or MIDI in (not sent out), or the arpeggiator / sequencer (sent out) */

static void voice_on(uint32_t note, uint32_t vel, uint32_t src)
{
    uint32_t v, pick = 0, best = 0xFFFFFFFFu, i, j;
    if (note > 127u || !vel) return;
    if (src != SRC_KEY) mout_push(0x90u, note, vel);
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
                       : voice[v].stage == ST_OFF && !voice[v].gate ? 1u         /* (a voice claimed a moment ago by another note of the same chord still reads ST_OFF until the audio runs) */
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
static void voice_off(uint32_t note, uint32_t src)
{
    uint32_t v, i, j;
    if (src != SRC_KEY) mout_push(0x80u, note, 0);
    if (is_mono()) {
        for (i = j = 0; i < nstk; i++) if (stk[i] != note) stk[j++] = stk[i];
        nstk = (uint8_t)j;
        if (nstk) {                             /* back to the previous held key, legato */
            voice[0].note = stk[nstk - 1u];
            voice[0].inc_t = note_inc(stk[nstk - 1u]);
        } else {
#ifdef DINGHY_HOST
            if (voice[0].gate) printf("%6u ms  voice off %3u\n", fm1_ms, note);
#endif
            voice[0].gate = 0;
        }
        return;
    }
    for (v = 0; v < NV; v++)
        if (voice[v].note == note) {
#ifdef DINGHY_HOST
            if (voice[v].gate) printf("%6u ms  voice off %3u\n", fm1_ms, note);    /* (one line per voice released) */
#endif
            voice[v].gate = 0;
        }
}
static void voice_panic(void)
{
    uint32_t v;
    nstk = 0;
    dr_t = 0;
    for (v = 0; v < NV; v++) voice[v].gate = 0;
}
static int quiet(void)
{
    uint32_t v;
    for (v = 0; v < NV; v++) if (voice[v].stage != ST_OFF) return 0;
    return 1;
}

/* SCALE and ROOT: every note that starts (keys, MIDI, the arpeggiator, the sequencer) snaps to the nearest note of the scale */
static const uint16_t SCALE_MASK[9] = {0xFFF, 0xAB5, 0x5AD, 0x6AD, 0x5AB, 0x9B3, 0x4A9, 0x295, 0x555};   /* off major minor dorian phrygian bhairav pent- pent+ whole tone */
static uint32_t clamp_note(int32_t n) { return n < 1 ? 1u : n > 127 ? 127u : (uint32_t)n; }
static uint32_t quant_note(uint32_t note)
{
    uint32_t sc = (uint32_t)PE[P_SCALE] * 9u >> 7, mask, root, rel, d;
    if (!sc) return note;
    mask = SCALE_MASK[sc];
    root = (uint32_t)PE[P_ROOT] * 12u >> 7;
    rel = (note + 120u - root) % 12u;
    for (d = 0; d < 7u; d++) {
        if (mask >> ((rel + 12u - d) % 12u) & 1u) return clamp_note((int32_t)note - (int32_t)d);
        if (mask >> ((rel + d) % 12u) & 1u) return clamp_note((int32_t)note + (int32_t)d);
    }
    return note;
}
static uint8_t qnote[128];                      /* the note a key actually started, for its release */

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

static void arp_build(void)                     /* the notes of the chord, low to high, across the octave range */
{
    const uint32_t *src = arp_cmd == 2u ? lmask : pmask;
    uint32_t octs = 1u + ((uint32_t)PE[P_ARPOCT] * 3u >> 7), o, nt, n = 0;
    for (o = 0; o < octs; o++)
        for (nt = 0; nt < 128u && n < 32u; nt++)
            if ((src[nt >> 5] >> (nt & 31u) & 1u) && nt + 12u * o <= 127u) alist[n++] = (uint8_t)(nt + 12u * o);
    an = n;
    octs_seen = octs;
    if (aidx >= an) aidx = 0;
}

static void arp_fire(void)
{
    uint32_t mode = PE[P_ARPMODE] >> 5, pos = aidx;
    if (arp_cur) { voice_off(arp_cur, SRC_AUTO); arp_cur = 0; }
    if (!an) return;
    if (pos >= an) pos = 0;
    arp_cur = quant_note(alist[pos]);
    voice_on(arp_cur, 100, SRC_AUTO);
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
    uint32_t q = quant_note(note);
    if (arp_cmd == 2u && !(pmask[0] | pmask[1] | pmask[2] | pmask[3]))
        lmask[0] = lmask[1] = lmask[2] = lmask[3] = 0;            /* HOLD: a new chord replaces the old one */
    pmask[note >> 5] |= 1u << (note & 31u);
    lmask[note >> 5] |= 1u << (note & 31u);
    qnote[note & 127u] = (uint8_t)q;
    arp_dirty = 1;
    if (rec_arm) rec_note(note, vel);
    if (!arp_cmd) voice_on(q, vel, SRC_KEY);
}
static void user_off(uint32_t note)
{
    pmask[note >> 5] &= ~(1u << (note & 31u));
    arp_dirty = 1;
    if (!arp_cmd) voice_off(qnote[note & 127u], SRC_KEY);
}

static void engine_tick(void)                   /* every 16 frames */
{
    uint32_t step_len, arp_len, gl, bpm, swd, ac, wp, ra, i;
    static const uint8_t ARPK[4] = {32, 16, 11, 8};             /* 1/8, 1/16, 1/16 triplet, 1/32 in 16ths of a step */

    bpm = 60u + PE[P_BPM];
    step_len = 661500u / bpm;                   /* samples per 16th note: 44100 * 60 / 4 / bpm */
    swd = (step_len * PE[P_SWING]) >> 9;        /* the odd steps come late, up to a quarter step */
    arp_len = (step_len * ARPK[PE[P_ARPRATE] >> 5]) >> 4;
    gl = 16u + ((uint32_t)PE[P_GATE] * 112u >> 7);              /* gate 12..99 % in 128ths */
    seq_len = 1u + (PE[P_LEN] >> 2);
    xp_cache = (int32_t)PE[P_XPOSE] - 64;
    xp_cache = xp_cache > 24 ? 24 : xp_cache < -24 ? -24 : xp_cache;

    while (evq_r != evq_w) {                    /* events from the keys and MIDI */
        uint32_t e = evq[evq_r & (EVQ - 1u)], type = e >> 14, vel = e >> 7 & 127u, note = e & 127u;
        evq_r++;
        if (type == EV_ON) user_on(note, vel);
        else if (type == EV_OFF) user_off(note);
        else if (type == EV_PANIC) {
            mout_push(0xB0u, 123, 0);               /* all notes off, to whatever listens on the USB out */
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
            for (i = 0; i < 128u; i++) if (pmask[i >> 5] >> (i & 31u) & 1u) voice_off(qnote[i], SRC_KEY);
        }
        if (!ac && arp_cur) { voice_off(arp_cur, SRC_AUTO); arp_cur = 0; }
        prev_arp = ac;
        arp_dirty = 1;
    }
    if (arp_dirty || octs_seen != 1u + ((uint32_t)PE[P_ARPOCT] * 3u >> 7)) { arp_dirty = 0; arp_build(); }

    wp = want_play;
    if (wp && !playing) {                       /* start: the first step plays at once */
        seq_step = seq_len - 1u;
        seq_cur_len = step_len + (((seq_len - 1u) & 1u) ? 0u - swd : swd);
        seq_acc = seq_cur_len;
        playing = 1;
    } else if (!wp && playing) {
        playing = 0;
        if (seq_cur) { voice_off(seq_cur, SRC_AUTO); seq_cur = 0; }
    }

    if (ac) {                                   /* the arpeggiator */
        if (!an) { arp_acc = arp_len; an_prev = 0; }
        else {
            if (!an_prev) { aidx = PE[P_ARPMODE] >> 5 == 1u ? an - 1u : 0; adir = 1; arp_acc = arp_len; }
            an_prev = an;
            arp_acc += 16u;
            if (arp_acc >= arp_len) {
                arp_acc -= arp_len;
                if (arp_acc >= arp_len) arp_acc = 0;
                arp_fire();
                arp_off_at = (arp_len * gl) >> 7;
            } else if (arp_cur && arp_acc >= arp_off_at) { voice_off(arp_cur, SRC_AUTO); arp_cur = 0; }
        }
    }

    if (playing) {                              /* the sequencer */
        seq_acc += 16u;
        if (seq_acc >= seq_cur_len) {
            uint32_t s, n;
            seq_acc -= seq_cur_len;
            if (seq_acc >= step_len) seq_acc = 0;
            if (seq_cur) { voice_off(seq_cur, SRC_AUTO); seq_cur = 0; }
            s = seq_step + 1u >= seq_len ? 0u : seq_step + 1u;
            seq_step = s;
            seq_cur_len = step_len + ((s & 1u) ? 0u - swd : swd);
            seq_off_at = (seq_cur_len * gl) >> 7;
            n = pat[s];
            if (n & 127u) { seq_cur = quant_note(clamp_note((int32_t)(n & 127u) + xp_cache)); voice_on(seq_cur, n & 128u ? 127u : 90u, SRC_AUTO); }
        } else if (seq_cur && seq_acc >= seq_off_at) { voice_off(seq_cur, SRC_AUTO); seq_cur = 0; }
    }
}

/* ---- settings: the patch slots, the globals, the pattern and the octave, packed 7 bits to a value into the 256 bytes */
#define SET_MAGIC 0xE1u                         /* (the layout of every earlier TripDeck and CyberDeck differs) */
static struct { uint8_t cur; int8_t oct; uint8_t slot[NPRESET][NPP], g[NP - NPP], pat[32]; } set;

static void pack7(uint8_t *d, const uint8_t *s, uint32_t n)
{
    uint32_t acc = 0, bits = 0, o = 0, i;
    for (i = 0; i < n; i++) {
        acc |= (uint32_t)(s[i] & 127u) << bits;
        bits += 7u;
        while (bits >= 8u) { d[o++] = (uint8_t)acc; acc >>= 8; bits -= 8u; }
    }
    if (bits) d[o] = (uint8_t)acc;
}
static void unpack7(uint8_t *d, const uint8_t *s, uint32_t n)
{
    uint32_t acc = 0, bits = 0, o = 0, i;
    for (i = 0; i < n; i++) {
        while (bits < 7u) { acc |= (uint32_t)s[o++] << bits; bits += 8u; }
        d[i] = (uint8_t)(acc & 127u);
        acc >>= 7; bits -= 7u;
    }
}
#define SLOTB 25u                               /* 28 values x 7 bits = 196 bits = 25 bytes */
static void settings_store(void)                /* 4 + 8 x 25 + 11 + 32 = 247 bytes */
{
    uint8_t raw[256];
    uint32_t i;
    for (i = 0; i < 256u; i++) raw[i] = 0;
    raw[0] = SET_MAGIC; raw[1] = set.cur; raw[2] = (uint8_t)(set.oct + 2);
    for (i = 0; i < NPRESET; i++) pack7(raw + 4u + i * SLOTB, set.slot[i], NPP);
    pack7(raw + 4u + NPRESET * SLOTB, set.g, NP - NPP);
    for (i = 0; i < 32u; i++) raw[4u + NPRESET * SLOTB + 11u + i] = set.pat[i];
    settings_save(raw, 256u);
}
static int settings_fetch(void)                 /* 0: loaded */
{
    uint8_t raw[256];
    uint32_t i;
    if (settings_load(raw, 256u) != 0 || raw[0] != SET_MAGIC || raw[1] >= NPRESET || raw[2] > 4u) return -1;
    set.cur = raw[1]; set.oct = (int8_t)((int32_t)raw[2] - 2);
    for (i = 0; i < NPRESET; i++) unpack7(set.slot[i], raw + 4u + i * SLOTB, NPP);
    unpack7(set.g, raw + 4u + NPRESET * SLOTB, NP - NPP);
    for (i = 0; i < 32u; i++) set.pat[i] = raw[4u + NPRESET * SLOTB + 11u + i];
    return 0;
}

static uint32_t cur;                            /* current preset */
static uint32_t edited;                         /* edited since loaded */

static void load_preset(uint32_t n)
{
    uint32_t i;
    push_ev(EV_PANIC, 0, 0);
    cur = n;
#ifdef DINGHY_HOST
    printf("%6u ms  preset index %u\n", fm1_ms, n);
#endif
    for (i = 0; i < NPP; i++) P[i] = set.slot[n][i];
    edited = 0;
}
static void set_param(uint32_t idx, int32_t val)
{
    val = val < 0 ? 0 : val > 127 ? 127 : val;
    if (P[idx] != val) { P[idx] = (uint8_t)val; if (idx < NPP) edited = 1; }   /* (the global ones are not part of a preset) */
}

/* ---- the screen: a rainbow-neon facade. Gradient meters, a header with a level meter and a moving rainbow rule, a filled scope. */
#define BG RGB(8, 5, 20)
#define C_CY RGB(0, 235, 255)
#define C_MG RGB(255, 0, 160)
#define C_YE RGB(255, 225, 0)
#define C_PU RGB(110, 84, 170)
#define C_GR RGB(28, 20, 56)
#define C_TX RGB(210, 205, 240)
#define KEY0 53u

static uint16_t hue565(uint32_t ph, uint32_t br)    /* a rainbow colour: ph 0..255 once round the wheel, br 0..255 */
{
    uint32_t r = (uint32_t)(128 + SIN8[ph & 255u]), g = (uint32_t)(128 + SIN8[(ph + 85u) & 255u]), b = (uint32_t)(128 + SIN8[(ph + 170u) & 255u]);
    return RGB(r * br >> 8, g * br >> 8, b * br >> 8);
}

static const char *plabel(uint32_t idx)          /* the OSC knobs mean different things for each voice */
{
    static const char *const OL[4][3] = {{"PULSE W", "DETUNE", "SUB"}, {"BUZZ", "MEEND", "DRONE"}, {"DRAWBAR", "LESLIE", "16 FOOT"}, {"RATIO", "INDEX", "TINE"}};
    return idx > P_VOICE && idx < P_CUT ? OL[ENG()][idx - 1u] : LABEL[idx];
}

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
    static const char *const EN[4] = {"PAD", "SITAR", "ORGAN", "BELL"}, *const VW[5] = {"A", "E", "I", "O", "U"};
    static const char *const SCL[9] = {"OFF", "MAJOR", "MINOR", "DORIAN", "PHRYG", "BHAIRAV", "PENT-", "PENT+", "WHOLE"};
    static const char *const RT[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    static const char *const AM[4] = {"UP", "DOWN", "UP-DN", "RAND"}, *const AR[4] = {"1/8", "1/16", "1/16T", "1/32"};
    const char *p = 0;
    uint32_t v = PE[idx];
    if (idx == P_LSHAPE) p = SHP[v >> 5];
    else if (idx == P_LDEST) p = DST[v * 3u >> 7];
    else if (idx == P_VOICE) p = EN[ENG()];
    else if (idx == P_ARPMODE) p = AM[v >> 5];
    else if (idx == P_ARPRATE) p = AR[v >> 5];
    else if (idx == P_VOWEL) p = VW[(v + 16u) >> 5];
    else if (idx == P_SCALE) p = SCL[v * 9u >> 7];
    else if (idx == P_ROOT) p = RT[v * 12u >> 7];
    else if (idx == P_FREEZE) p = v >= 64u ? "ON" : "OFF";
    else if (idx == P_GLIDE && v == 0u) p = "POLY";
    if (p) { do { *s++ = *p; } while (*p++); return; }
    if (idx == P_ARPOCT) v = 1u + (v * 3u >> 7);
    else if (idx == P_GATE) v = (16u + (v * 112u >> 7)) * 100u >> 7;
    else if (idx == P_BPM) v = 60u + v;
    else if (idx == P_SWING) v = 50u + v * 25u / 127u;
    else if (idx == P_LEN) v = 1u + (v >> 2);
    if (idx == P_XPOSE) { int32_t x = (int32_t)v - 64; itoa3(x > 24 ? 24 : x < -24 ? -24 : x, s); return; }
    itoa3(idx == P_ENVAMT || idx == P_SPIRAL || idx == P_SHIFT ? (int32_t)v - 64 : (int32_t)v, s);
}

static void draw_head(void)                     /* the title, a level meter, a moving rainbow rule, the patch and the transport */
{
    char s[24], bpm[8];
    uint32_t n = 0, i, lit = vis_pk * 12u >> 15, t = fm1_ms >> 3;
    const char *p = PNAME[cur], *arp = arp_cmd == 2u ? "HLD" : "ARP";
    int32_t x;
    lit = lit > 12u ? 12u : lit;
    cv_begin(240, 56, BG);
    cv_text(10, 0, &FONT_L, "TRIPDECK", C_MG);      /* chromatic aberration: magenta, cyan, then white on top */
    cv_text(6, 0, &FONT_L, "TRIPDECK", C_CY);
    cv_text(8, 0, &FONT_L, "TRIPDECK", C_WHITE);
    for (i = 0; i < 12u; i++) {                     /* the output level: twelve bars that grow */
        int32_t h = 5 + (int32_t)i * 2;
        cv_rect(180 + (int32_t)i * 5, 31 - h, 4, h, i < lit ? hue565(i * 12u + t, 255u) : C_GR);
    }
    for (x = 0; x < 240; x++) cv_rect(x, 34, 1, 3, hue565((uint32_t)x + t, 230u));
    s[0] = (char)('1' + cur); s[1] = 0;
    cv_rect(6, 38, 18, 16, hue565(cur * 32u, 255u));
    cv_text(11, 38, &FONT_S, s, BG);
    while (*p && n < 20u) s[n++] = *p++;
    if (edited) s[n++] = '*';
    s[n] = 0;
    cv_text(30, 38, &FONT_S, s, C_YE);
    itoa3(60 + PE[P_BPM], bpm);                     /* right side: tempo, arp, transport, record */
    x = 236 - text_w(&FONT_S, bpm) - (arp_cmd ? text_w(&FONT_S, arp) + 8 : 0) - (playing || want_play ? 17 : 0)
        - (rec_arm ? text_w(&FONT_S, "REC") + 8 : 0);
    x = cv_text(x, 38, &FONT_S, bpm, C_TX) + 8;
    if (arp_cmd) x = cv_text(x, 38, &FONT_S, arp, C_CY) + 8;
    if (playing || want_play) x = cv_text(x, 38, &FONT_S, ">", C_CY) + 8;
    if (rec_arm) cv_text(x, 38, &FONT_S, "REC", C_MG);
    cv_blit(0, 0);
}

static void draw_seq(void)                      /* the pattern as a row of steps: bar height = pitch, yellow = accent */
{
    uint32_t s, len = 1u + (PE[P_LEN] >> 2), mn = 255, mx = 0, curs = playing ? seq_step : 255u, rc = rec_arm && !playing ? rec_cursor : 255u;
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
            cv_rect(x + 2, y + 26 - h, 9, h, pat[s] & 0x80u ? C_YE : s == curs ? C_WHITE : hue565(n * 6u, 255u));
        } else if (s == rc)
            cv_rect(x + 3, y + 12, 7, 3, C_MG);
    }
    cv_blit(0, 56);
}

static void draw_scope(void)                    /* the waveform, filled to the centre line with a rainbow that slides */
{
    uint32_t end = scope_w, a = end - 1500u, i, start = a, t = fm1_ms >> 4;
    int32_t x, y;
    cv_begin(240, 64, BG);
    for (x = 0; x < 240; x += 30) cv_rect(x, 0, 1, 64, C_GR);
    cv_rect(0, 32, 240, 1, C_GR);
    for (i = 0; i < 600u; i++)                  /* trigger on a rising zero crossing */
        if (scope[(a + i) & (SCOPE_N - 1u)] < 0 && scope[(a + i + 1u) & (SCOPE_N - 1u)] >= 0) { start = a + i; break; }
    for (x = 0; x < 240; x++) {
        int32_t y0;
        y = 32 - ((int32_t)scope[(start + (uint32_t)x * 3u) & (SCOPE_N - 1u)] * 28 >> 15);
        y0 = y < 32 ? y : 32;
        cv_rect(x, y0, 1, (y < 32 ? 32 - y : y - 32) + 1, hue565((uint32_t)x + t, 110u));
        cv_rect(x, y - 1, 1, 3, hue565((uint32_t)x + t, 255u));
    }
    cv_rect(0, 0, 14, 1, C_MG); cv_rect(0, 0, 1, 8, C_MG); cv_rect(226, 0, 14, 1, C_MG); cv_rect(239, 0, 1, 8, C_MG);
    cv_rect(0, 63, 14, 1, C_MG); cv_rect(0, 56, 1, 8, C_MG); cv_rect(226, 63, 14, 1, C_MG); cv_rect(239, 56, 1, 8, C_MG);
    cv_blit(0, 56);
}

static void seg_meter(int32_t x, int32_t y, uint32_t v, uint32_t nseg, uint32_t sw, uint32_t hue0, uint32_t hot)   /* segments, a rainbow along them */
{
    uint32_t i, lit = (v * nseg + 126u) / 127u;
    for (i = 0; i < nseg; i++)
        cv_rect(x + (int32_t)(i * (sw + 1u)), y, (int32_t)sw, 11, i < lit ? (hot && i + 1u == lit ? C_WHITE : hue565(hue0 + i * 7u, 255u)) : C_GR);
}

static void draw_row(uint32_t page, uint32_t r, uint32_t sel)
{
    char s[12];
    uint32_t idx = page * 4u + r, v = PE[idx], hot = r == sel;
    cv_begin(240, 20, BG);
    if (hot) cv_rect(0, 2, 3, 16, C_YE);
    cv_text(8, 2, &FONT_S, plabel(idx), hot ? C_YE : C_CY);
    seg_meter(108, 4, v, 16, 5, page * 25u + 150u, hot);
    fmt_val(idx, s);
    cv_text(236 - text_w(&FONT_S, s), 2, &FONT_S, s, C_WHITE);
    cv_blit(0, 122 + r * 20u);
}

static void draw_tabs(uint32_t page)            /* ten pages: the one you are on is a rainbow pill with its name, the rest are bars */
{
    uint32_t t;
    int32_t x = 0;
    cv_begin(240, 16, BG);
    for (t = 0; t < 10u; t++) {
        int32_t w = t == page ? 51 : 20;
        if (t == page) {
            cv_rect(x, 0, w, 16, hue565(t * 25u + 20u, 255u));
            cv_pset(x, 0, BG); cv_pset(x + w - 1, 0, BG); cv_pset(x, 15, BG); cv_pset(x + w - 1, 15, BG);
            cv_text(x + w / 2 - text_w(&FONT_S, TAB[t]) / 2, 0, &FONT_S, TAB[t], BG);
        } else
            cv_rect(x + 1, 6, w - 2, 5, hue565(t * 25u + 20u, 110u));
        x += w + 1;
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

static const uint8_t CC_MAP[6][2] = {{74, P_CUT}, {71, P_RES}, {73, P_ATK}, {72, P_REL}, {1, P_LDEPTH}, {91, P_RVB}};
/* MUTATE (double-tap HOME): a random walk of the patch. Each knob that may move has limits that keep the patch playable:
 * no eight-second attacks, no runaway feedback, the filter never fully shut. The voice and FREEZE are left alone. */
static const struct { uint8_t idx, lo, hi; } MUT[] = {
    {P_O1, 5, 120}, {P_O2, 0, 120}, {P_O3, 0, 120},
    {P_CUT, 35, 127}, {P_RES, 0, 105}, {P_ENVAMT, 0, 127}, {P_DRV, 0, 110},
    {P_ATK, 0, 90}, {P_DEC, 20, 127}, {P_SUS, 0, 127}, {P_REL, 10, 110},
    {P_LRATE, 0, 127}, {P_LDEPTH, 0, 110}, {P_LDEST, 0, 127}, {P_LSHAPE, 0, 127},
    {P_DMIX, 0, 90}, {P_DTIME, 0, 127}, {P_DFB, 0, 100}, {P_SPIRAL, 40, 88},
    {P_RVB, 0, 110}, {P_SHIM, 0, 90}, {P_DAMP, 0, 127},
    {P_VOWEL, 0, 127}, {P_TALK, 0, 100}, {P_SHIFT, 40, 88}, {P_SWEEP, 0, 100}};
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

/* TRIP: the knobs that colour the sound drift on their own, each a slow wandering walk, as far as TRIP allows (PD is added to P) */
static const uint8_t TRIPSET[] = {P_O1, P_O2, P_O3, P_CUT, P_RES, P_LDEPTH, P_DFB, P_SHIM, P_DAMP, P_VOWEL, P_SHIFT};
#define NTRIP (sizeof TRIPSET)
static int8_t tvel[NTRIP];
static void trip_step(void)                     /* every 80 ms */
{
    int32_t amp = (int32_t)P[P_TRIP] >> 1, i;
    for (i = 0; i < (int32_t)NTRIP; i++) {
        int32_t d = PD[TRIPSET[i]], v = tvel[i] + (int32_t)(mrand() % 3u) - 1;
        v = v > 2 ? 2 : v < -2 ? -2 : v;
        d += v;
        if (d > amp) { d = amp; v = -1; } else if (d < -amp) { d = -amp; v = 1; }
        PD[TRIPSET[i]] = (int8_t)d; tvel[i] = (int8_t)v;
    }
}

static const uint8_t PAT0[32] = {0xAD, 0, 45, 0, 48, 0, 45, 0, 43, 0, 43, 45, 0, 50, 0, 48};   /* a default line in A minor */

#include "scene_vis.h"

/* ---- start up: the settings and the pictures are set up while the loading screen runs, one stage per label */
static void init_stage(uint32_t n)
{
    uint32_t k;
    if (n == 0u) {                              /* SETTINGS */
        if (settings_fetch() != 0) {
            uint32_t i, j;
            set.cur = 0; set.oct = 0;
            for (i = 0; i < NPRESET; i++) for (j = 0; j < NPP; j++) set.slot[i][j] = FACTORY[i][j];
            for (i = 0; i < NP - NPP; i++) set.g[i] = GLOBAL0[i];
            for (i = 0; i < 32u; i++) set.pat[i] = PAT0[i];
        }
    } else if (n == 1u) {                       /* OSCILLATORS: the patch, the globals, the pattern, and clean delay memory */
        for (k = 0; k < NP - NPP; k++) P[NPP + k] = set.g[k];
        for (k = 0; k < 32u; k++) pat[k] = set.pat[k];
        load_preset(set.cur);
        for (k = 0; k < NP; k++) PE[k] = P[k];
        memset(rvl, 0, sizeof rvl);
        memset(ksl, 0, sizeof ksl);
        memset(psb, 0, sizeof psb);
    } else                                      /* SCENES */
        vis_init();
}

#ifdef DINGHY_HOST
static void host_set_param(uint32_t idx, uint32_t val) { if (idx < NP) { P[idx] = (uint8_t)val; if (idx < NPP) edited = 1; } }
#endif

#define SPLASH_MS 2200u
static void boot_splash(void)                   /* the logo, a rising wave and a progress bar; any button skips it */
{
    static const char *const STEP[4] = {"SETTINGS", "OSCILLATORS", "PICTURES", "READY"};
    uint32_t t0 = fm1_ms, dur, done = 0, t_draw = 0, rel;
#ifdef DINGHY_HOST
    dur = host_splash_ms;                       /* (the tests have none unless the script asks: "2200 splash") */
#else
    dur = SPLASH_MS;
#endif
    lcd_fill(0, 0, 240, 240, BG);
    if (dur) {
        int32_t lx = 120 - text_w(&FONT_L, "TRIPDECK") / 2;
        cv_begin(240, 40, BG);
        cv_text(lx + 3, 4, &FONT_L, "TRIPDECK", C_MG);
        cv_text(lx - 3, 4, &FONT_L, "TRIPDECK", C_CY);
        cv_text(lx, 4, &FONT_L, "TRIPDECK", C_WHITE);
        cv_blit(0, 34);
        cv_begin(240, 44, BG);
        cv_text(120 - text_w(&FONT_S, "PSYCHEDELIC SYNTH") / 2, 0, &FONT_S, "PSYCHEDELIC SYNTH", C_YE);
        cv_text(120 - text_w(&FONT_S, "FOR THE M-VAVE FM-1") / 2, 22, &FONT_S, "FOR THE M-VAVE FM-1", C_PU);
        cv_blit(0, 84);
    }
    while (dur) {
        uint32_t e, p, step, x, w;
        platform_poll();
        e = fm1_ms - t0;
        if (e >= dur || fm1_input_edges(&rel)) break;
        p = e * 118u / dur;
        p = p > 100u ? 100u : p;
        step = p >= 100u ? 3u : p * 3u / 100u;
        while (done < 3u && done <= step) init_stage(done++);
        if (e - t_draw < 30u && e > 2u) continue;
        t_draw = e;
        cv_begin(240, 44, BG);                  /* a wave of bars that grows with the load */
        for (x = 0; x < 240u; x += 6u) {
            uint32_t m = ((uint32_t)(128 + SIN8[(x * 3u + e / 3u) & 255u]) * (uint32_t)(128 + SIN8[(x * 2u + e / 2u) & 255u])) >> 11;
            uint32_t h = 3u + m * (30u + p * 70u / 100u) / 100u;
            cv_rect((int32_t)x, 22 - (int32_t)(h / 2u), 5, (int32_t)h, hue565(x + e / 4u, 255u));
        }
        cv_blit(0, 140);
        cv_begin(240, 44, BG);
        cv_text(120 - text_w(&FONT_S, STEP[step]) / 2, 0, &FONT_S, STEP[step], C_TX);
        cv_rect(20, 24, 200, 14, C_PU);
        cv_rect(21, 25, 198, 12, BG);
        w = p * 196u / 100u;
        for (x = 0; x < w; x++) cv_rect(22 + (int32_t)x, 26, 1, 10, hue565(x + e / 6u, 255u));
        cv_blit(0, 192);
    }
    while (done < 3u) init_stage(done++);       /* skipped, or no splash: the same start-up */
}

static void app_main(void)
{
    uint32_t prev = 0, k, eng_drawn = 9, t_scope = 0, t_head = 0, t_stat = 0, t_change = 0, t_trip = 0, redraw = 1, page = 0, sel = 0, sel_drawn = 0;
    uint32_t pending_save = 0, was_mono, lit_drawn = ~0u, edited_drawn = 0, sig_drawn = ~0u, home_t = 0, home_long = 0, tap_t = 0;
    uint32_t show_vis = 1, vis_drawn = 0;
    uint8_t shown[NP];
    int32_t oct_drawn = 99;
    uint8_t held[27] = {0};
    const char *msg = 0;
    uint32_t t_msg = 0;

#ifdef DINGHY_HOST
    host_param_cb = host_set_param;             /* (the tests set parameters by number) */
#endif
    boot_splash();
    was_mono = (uint32_t)is_mono();
    for (k = 0; k < NP; k++) shown[k] = 255;

    lcd_fill(0, 0, 240, 240, BG);

    for (;;) {
        uint32_t keys, down, up, rel, b, lit = 0, base = (uint32_t)(KEY0 + 12 * set.oct), r, sig, np;
        uint8_t st, d1, d2;
        int32_t d;
        platform_poll();

        keys = fm1_in.notes;
        down = keys & ~prev;
        up = prev & ~keys;
        prev = keys;
        for (k = 0; k < 27u; k++) {
            if (down >> k & 1u) { held[k] = (uint8_t)(base + k); push_ev(EV_ON, held[k], 100); if (!arp_cmd) midi_send(0x90, held[k], 100); }   /* (with the arp on, the arp's notes go out instead) */
            if (up >> k & 1u) { push_ev(EV_OFF, held[k], 0); midi_send(0x80, held[k], 0); }
        }

        b = fm1_input_edges(&rel);
        np = page;                              /* the page buttons */
        if (b & (1u << BTN_EDIT)) np = page == 0u ? 1u : 0u;
        if (b & (1u << BTN_ENV)) np = 2u;
        if (b & (1u << BTN_LFO)) np = 3u;
        if (b & (1u << BTN_FX)) np = page == 4u ? 5u : page == 5u ? 6u : 4u;
        if (b & (1u << BTN_GLO)) np = 7u;
        if (b & (1u << BTN_SEL)) np = page + 1u >= NPAGE ? 0u : page + 1u;
        if (b & (1u << BTN_SEQ)) np = 9u;
        if (b & (1u << BTN_ARP)) {              /* ARP: its page (and the arp on); again: on -> hold -> off */
            if (page != 8u) { np = 8u; if (!arp_cmd) arp_cmd = 1; }
            else arp_cmd = (uint8_t)(arp_cmd == 0u ? 1u : arp_cmd == 1u ? 2u : 0u);
        }
        if (np != page) { page = np; vstrip = 0; vtouch = 0; redraw = 1; }
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
        if (rel & (1u << BTN_HOME)) {           /* tapped: a rest while step recording; twice: mutate; once, if no second tap comes: the picture <-> the parameters */
            if (home_t && !home_long) {
                if (rec_arm && !playing) push_ev(EV_REST, 0, 0);
                else if (tap_t && fm1_ms - tap_t < 320u) { tap_t = 0; mutate(); msg = "MUTATED"; t_msg = fm1_ms; }
                else tap_t = fm1_ms;
            }
            home_t = 0;
        }
        if (tap_t && fm1_ms - tap_t >= 320u) { tap_t = 0; show_vis ^= 1u; vstrip = 0; vtouch = 0; redraw = 1; }
        if (b & (1u << BTN_OCT_DOWN) && set.oct > -2) { set.oct--; pending_save = 1; t_change = fm1_ms; }
        if (b & (1u << BTN_OCT_UP) && set.oct < 2) { set.oct++; pending_save = 1; t_change = fm1_ms; }
        if (b & (1u << BTN_SAVE)) {             /* the preset slot, the global settings and the pattern */
            for (k = 0; k < NPP; k++) set.slot[cur][k] = P[k];
            for (k = 0; k < NP - NPP; k++) set.g[k] = P[NPP + k];
            for (k = 0; k < 32u; k++) set.pat[k] = pat[k];
            set.cur = (uint8_t)cur; edited = 0; pending_save = 1; t_change = fm1_ms; redraw = 1;
            msg = "SAVED"; t_msg = fm1_ms;
        }

        for (k = 0; k < 4u; k++) {              /* the four knobs of the page: the sound and the picture change together */
            d = fm1_enc_take(ENC_KNOB1 + k);
            if (d) {
                set_param(page * 4u + k, (int32_t)P[page * 4u + k] + d * (d > 2 || d < -2 ? 6 : 2));
                sel = k; vtouch_ms[k] = fm1_ms; vtouch = k + 1u;
            }
        }
        d = fm1_enc_take(ENC_ALGORITHM);
        if (d) { set_param(P_CUT, (int32_t)P[P_CUT] + d * (d > 2 || d < -2 ? 6 : 2)); if (show_vis) { msg = "CUTOFF"; t_msg = fm1_ms; } }
        d = fm1_enc_take(ENC_SELECT);
        if (d) { set_param(P_RES, (int32_t)P[P_RES] + d * (d > 2 || d < -2 ? 6 : 2)); if (show_vis) { msg = "RESONANCE"; t_msg = fm1_ms; } }
        d = fm1_enc_take(ENC_PRESETS);
        if (d) {
            int32_t n = (int32_t)cur + (d > 0 ? 1 : -1);
            load_preset((uint32_t)(n < 0 ? (int32_t)NPRESET - 1 : n >= (int32_t)NPRESET ? 0 : n));
            redraw = 1;
            if (show_vis) { msg = PNAME[cur]; t_msg = fm1_ms; }
        }
        if ((uint32_t)is_mono() != was_mono) { was_mono = (uint32_t)is_mono(); push_ev(EV_PANIC, 0, 0); }

        if (fm1_ms - t_trip >= 80u) { t_trip = fm1_ms; trip_step(); }
        for (k = 0; k < NP; k++) { int32_t v = (int32_t)P[k] + PD[k]; PE[k] = (uint8_t)(v < 0 ? 0 : v > 127 ? 127 : v); }

        while (midi_get(&st, &d1, &d2)) {       /* USB-MIDI and the TRS jack come out of the same queue */
            if ((st & 0xF0u) == 0x90u && d2) push_ev(EV_ON, d1, d2);
            else if ((st & 0xF0u) == 0x80u || (st & 0xF0u) == 0x90u) push_ev(EV_OFF, d1, 0);
            else if ((st & 0xF0u) == 0xB0u) {
                if (d1 == 120u || d1 == 123u) push_ev(EV_PANIC, 0, 0);          /* all sound off / all notes off */
                else for (k = 0; k < 6u; k++)
                    if (CC_MAP[k][0] == d1) set_param(CC_MAP[k][1], d2);
            } else if ((st & 0xF0u) == 0xC0u) {                                 /* program change: the patch */
                load_preset(d1 % NPRESET);
                redraw = 1;
                if (show_vis) { msg = PNAME[cur]; t_msg = fm1_ms; }
            } else if ((st & 0xF0u) == 0xE0u) {                                 /* the pitch wheel */
                bend = ((int32_t)d1 | (int32_t)d2 << 7) - 8192;
#ifdef DINGHY_HOST
                printf("%6u ms  pitch bend %d\n", fm1_ms, (int)bend);
#endif
            }
        }
        while (moq_r != moq_w) {                /* what the arp and the sequencer played, out over USB */
            uint32_t m = moq[moq_r & (MOQ - 1u)];
            moq_r++;
            midi_send(m & 0xFFu, m >> 8 & 0x7Fu, m >> 16 & 0x7Fu);
        }

        base = (uint32_t)(KEY0 + 12 * set.oct);
        for (k = 0; k < NV; k++)
            if (voice[k].gate && voice[k].note >= base && voice[k].note < base + 27u)
                lit |= 1u << (voice[k].note - base);
        if (lit != lit_drawn) {
            lit_drawn = lit;
            for (k = 0; k < 27u; k++) fm1_led_key(KEY_LED(k), lit >> k & 1u);
        }
        fm1_led_key(BTN_EDIT, page < 2u);
        fm1_led_key(BTN_ENV, page == 2u);
        fm1_led_key(BTN_LFO, page == 3u);
        fm1_led_key(BTN_FX, page >= 4u && page < 7u);
        fm1_led_key(BTN_GLO, page == 7u);
        fm1_led_key(BTN_SEL, !show_vis);        /* lit: the parameters are showing */
        fm1_led_key(BTN_ARP, arp_cmd != 0u);
        fm1_led_key(BTN_SEQ, page == 9u);
        fm1_led_key(BTN_PLAY, playing && (seq_step & 3u) == 0u);   /* blinks on the beat */
        fm1_led_key(BTN_REC, rec_arm);
        fm1_led_key(BTN_OCT_DOWN, set.oct < 0);
        fm1_led_key(BTN_OCT_UP, set.oct > 0);

        org_build();                            /* (only when DRAWBAR or 16 FOOT moved) */
        vnote = 0;                              /* a held key, for the pictures */
        for (k = 0; k < NV; k++) if (voice[k].gate) vnote = voice[k].note;
        if (msg && fm1_ms - t_msg > 1500u) { msg = 0; oct_drawn = 99; }
        vmsg = msg;
        if (vtouch && fm1_ms - vtouch_ms[vtouch - 1u] > 1500u) vtouch = 0;
        if (show_vis) {                         /* the picture: one strip of it per pass */
#ifdef DINGHY_HOST
            static uint32_t t_strip;
            if (fm1_ms - t_strip >= 8u) { t_strip = fm1_ms; vis_step(page); }     /* (about what the screen link manages) */
#else
            vis_step(page);
#endif
            vis_drawn = 1;
            if (pending_save && quiet() && !playing && fm1_ms - t_change > 1000u) { settings_store(); pending_save = 0; }
            continue;
        }
        if (vis_drawn) {                        /* back from the picture: the parameters again */
            vis_drawn = 0; lcd_fill(0, 0, 240, 240, BG); redraw = 1; oct_drawn = 99; t_stat = fm1_ms - 1000u;
        }
        /* the parameters: only what changed */
        sig = arp_cmd | (playing ? 4u : 0u) | (rec_arm ? 8u : 0u) | (edited ? 16u : 0u) | (uint32_t)PE[P_BPM] << 8;
        if (redraw) { draw_head(); t_head = fm1_ms; draw_tabs(page); for (k = 0; k < NP; k++) shown[k] = 255; redraw = 0; oct_drawn = 99; sig_drawn = sig; edited_drawn = edited; }
        if (ENG() != eng_drawn) { eng_drawn = ENG(); for (k = 0; k < 4u; k++) shown[k] = 255; }   /* the OSC knobs are renamed */
        for (r = 0; r < 4u; r++)
            if (shown[page * 4u + r] != PE[page * 4u + r]) {
                shown[page * 4u + r] = PE[page * 4u + r];
                draw_row(page, r, sel);
            }
        if (sel != sel_drawn) {                 /* the highlight follows the last turned knob */
            sel_drawn = sel;
            for (r = 0; r < 4u; r++) shown[page * 4u + r] = 255;
        }
        if (sig != sig_drawn || edited != edited_drawn || fm1_ms - t_head >= 70u) { sig_drawn = sig; edited_drawn = edited; t_head = fm1_ms; draw_head(); }
        if (fm1_ms - t_scope >= 50u) { t_scope = fm1_ms; if (page == 9u) draw_seq(); else draw_scope(); }
        if (set.oct != oct_drawn || fm1_ms - t_stat >= 500u) {
            oct_drawn = set.oct; t_stat = fm1_ms;
            draw_status(set.oct, msg);
        }

        if (pending_save && quiet() && !playing && fm1_ms - t_change > 1000u) {   /* an erase stalls the sound: only when silent */
            settings_store();
            pending_save = 0;
        }
    }
}
