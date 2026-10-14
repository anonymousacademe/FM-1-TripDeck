/* SPDX-License-Identifier: GPL-3.0-only */
/* The psychedelic pictures (main loop). Included by tripdeck.c after the UI code.
 *
 * Every page of the synth has its own picture, and the four knobs of the page are its four parameters: the knob changes the sound
 * AND the picture. The same four slots always mean shape, depth, motion, colour; the values are the page's parameters (PE).
 *
 *   OSC KALEIDO  FLT TUNNEL  ENV WAVES  LFO PLASMA  DLY MELT  REV ZONE  VOX FLOWER  GLO SPIRAL  ARP WARP  SEQ HORIZON
 *
 * The picture is 120 x 120 cells, each drawn 2 x 2 pixels, sent as eight strips of 15 cell rows (240 x 30 pixels): one strip per
 * pass of the main loop, so the keys and knobs are read every ~10 ms while a frame is on its way to the screen.
 * Colours come from a palette rebuilt every frame (hue from the colour knob). Everything you hear is also on the screen: CUTOFF sets
 * the brightness, RESO the contrast, REVERB a milky glow, the patch LFO swings the hue at its own rate, the held note sets the hue,
 * and each new note flashes the picture and kicks its motion (the same `kick` strikes the voices). */
#define VW 120u
#define VH 120u

static uint8_t vbuf[2][VW * VH] __attribute__((section(".pool")));   /* melt and warp: the picture so far */
static uint16_t vpal[256];                      /* the palette, already byte swapped for the screen */
static uint32_t vcur, vstrip, vlast_ms, vlevel, vnote, vrnd = 0x2545F491u, vtime[NPAGE][4];
static uint32_t vtouch_ms[4], vtouch;           /* when each knob was last turned */
static uint32_t vkick, vhn;                     /* the note strike (0..255), and the hue the held note has pulled the colours to */
static const char *vmsg;                        /* a short message for the top line */
static uint8_t quant[VW];                       /* warp: the pixel size as a lookup */
static uint8_t sang[96];
static uint16_t srad[96];
static uint16_t zsq[2][VW];                     /* zone: the squared distance from each centre along x */
static struct { int32_t a, b, c, cx1, cy1, cx2, cy2; uint32_t t1, t2, t3, t4; } V;

static uint32_t vrand(void) { vrnd ^= vrnd << 13; vrnd ^= vrnd >> 17; vrnd ^= vrnd << 5; return vrnd; }

static uint32_t scurve(uint32_t c) { return c < 128u ? (c * c) >> 7 : 255u - (((255u - c) * (255u - c)) >> 7); }

static void build_pal(uint32_t fade, uint32_t hue)  /* fade: index 0 is black and the brightness grows with the index */
{
    uint32_t i, bright = 110u + ((uint32_t)PE[P_CUT] * 146u >> 7), rs = (uint32_t)PE[P_RES] * 2u, fl = (vkick >> 2) + ((uint32_t)PE[P_RVB] >> 4);
    for (i = 0; i < 256u; i++) {
        uint32_t ph = fade ? i * 2u + hue : i + hue, r, g, b, br;
        r = scurve((uint32_t)(128 + SIN8[ph & 255u]));
        g = scurve((uint32_t)(128 + SIN8[(ph + 85u) & 255u]));
        b = scurve((uint32_t)(128 + SIN8[(ph + 170u) & 255u]));
        if (fade) { br = i ? 64u + (i * 191u >> 8) : 0u; r = r * br >> 8; g = g * br >> 8; b = b * br >> 8; }   /* any trail stays visible until it is gone */
        r = (r * (256u - rs) + scurve(scurve(r)) * rs) >> 8;        /* RESO: harder contrast, sharper colour */
        g = (g * (256u - rs) + scurve(scurve(g)) * rs) >> 8;
        b = (b * (256u - rs) + scurve(scurve(b)) * rs) >> 8;
        r = r * bright >> 8; g = g * bright >> 8; b = b * bright >> 8;   /* CUTOFF: a dark, closed picture to a bright, open one */
        if (i) { r += fl; g += fl; b += fl; }                       /* a note flashes everything; REVERB adds a milky glow */
        r = r > 255u ? 255u : r; g = g > 255u ? 255u : g; b = b > 255u ? 255u : b;
        vpal[i] = swap16(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    }
}

static void vis_init(void)
{
    uint32_t i;
    for (i = 0; i < 96u; i++) { sang[i] = (uint8_t)vrand(); srad[i] = (uint16_t)(16u + vrand() % 1300u); }
}

/* ---- melt: video feedback. The last picture is zoomed and turned a little, faded, and some sparks are added. */
static void melt_update(uint32_t k1, uint32_t k2, uint32_t k3, uint32_t k4, uint32_t pg)
{
    const uint8_t *src = vbuf[vcur];
    uint8_t *dst = vbuf[vcur ^ 1u];
    int32_t z = 256 + (((int32_t)k1 - 64) >> 3), rt = ((int32_t)k3 - 64) >> 2, ly, lx, i, n;
    uint32_t fade = 1u + ((127u - k2) >> 4), a0 = vtime[pg][0] >> 8, val;
    for (ly = 0; ly < (int32_t)VH; ly++) {
        int32_t v = ly - 60;
        for (lx = 0; lx < (int32_t)VW; lx++) {
            int32_t u = lx - 60, sx = 60 + ((u * z - v * rt) >> 8), sy = 60 + ((v * z + u * rt) >> 8);
            uint32_t c = ((uint32_t)sx < VW && (uint32_t)sy < VH) ? src[sy * (int32_t)VW + sx] : 0u;
            dst[ly * (int32_t)VW + lx] = (uint8_t)(c > fade ? c - fade : 0u);
        }
    }
    n = vnote ? 4 : 3;                          /* three orbiting sparks, and one for the note you hold */
    val = 170u + (k4 >> 2) + (vlevel >> 3);
    val = val > 255u ? 255u : val;
    for (i = 0; i < n; i++) {
        uint32_t ang = i == 3 ? (vnote * 7u) & 255u : (a0 + (uint32_t)i * 85u) & 255u;
        int32_t R = i == 3 ? 30 : 10 + ((SIN8[((vtime[pg][1] >> 8) + (uint32_t)i * 40u) & 255u] + 127) >> 3) + (int32_t)(vlevel >> 4);
        int32_t cx = 60 + ((SIN8[(ang + 64u) & 255u] * R) >> 7), cy = 60 + ((SIN8[ang & 255u] * R) >> 7);
        int32_t rr = 3 + (int32_t)(vlevel >> 6) + (int32_t)(vkick >> 5) + (i == 3 ? 2 : 0), dx, dy;
        for (dy = -rr; dy <= rr; dy++)
            for (dx = -rr; dx <= rr; dx++) {
                int32_t d2 = dx * dx + dy * dy, x = cx + dx, y = cy + dy, c;
                if (d2 > rr * rr || (uint32_t)x >= VW || (uint32_t)y >= VH) continue;
                c = (int32_t)val - d2 * 2;
                if (c > dst[y * (int32_t)VW + x]) dst[y * (int32_t)VW + x] = (uint8_t)c;
            }
    }
    vcur ^= 1u;
}

/* ---- warp: stars streaming out of the centre, the faster the longer their streaks; PIXEL makes the picture chunky */
static void warp_update(uint32_t k1, uint32_t k2, uint32_t k3)
{
    uint8_t *F = vbuf[0];
    uint32_t i, spd = 2u + (k3 >> 2) + (vlevel >> 5) + (vkick >> 4), fade = 2u + ((127u - k2) >> 4), blk = 1u + (k1 * 9u >> 7);
    for (i = 0; i < VW * VH; i++) F[i] = (uint8_t)(F[i] > fade ? F[i] - fade : 0u);
    for (i = 0; i < 96u; i++) {
        uint32_t r0 = srad[i], r1 = r0 + ((((r0 >> 3) + 6u) * spd) >> 4), steps = ((r1 - r0) >> 4) + 1u, j, ang = sang[i];
        if (r1 > 84u * 16u) { sang[i] = (uint8_t)vrand(); srad[i] = (uint16_t)(16u + vrand() % 64u); continue; }
        if (steps > 14u) steps = 14u;
        for (j = 0; j < steps; j++) {
            uint32_t rj = r0 + (r1 - r0) * j / steps, rc = rj >> 4, c;
            int32_t x = 60 + ((SIN8[(ang + 64u) & 255u] * (int32_t)rc) >> 7), y = 60 + ((SIN8[ang & 255u] * (int32_t)rc) >> 7);
            if ((uint32_t)x >= VW || (uint32_t)y >= VH) continue;
            c = 90u + rc * 2u;
            if (c > 255u) c = 255u;
            if (c > F[y * (int32_t)VW + x]) F[y * (int32_t)VW + x] = (uint8_t)c;
        }
        srad[i] = (uint16_t)r1;
    }
    if (vnote) {                                /* a held key puts a bright star on its own ray */
        uint32_t ang = (vnote * 7u) & 255u, r;
        for (r = 6; r < 70u; r += 2u) {
            int32_t x = 60 + ((SIN8[(ang + 64u) & 255u] * (int32_t)r) >> 7), y = 60 + ((SIN8[ang & 255u] * (int32_t)r) >> 7);
            if ((uint32_t)x < VW && (uint32_t)y < VH) F[y * (int32_t)VW + x] = 255;
        }
    }
    for (i = 0; i < VW; i++) quant[i] = (uint8_t)(i / blk * blk);
}

/* ---- the per-frame set-up: time, level, palette, and the pictures that keep a buffer */
static void vis_frame_begin(uint32_t pg)
{
    uint32_t dt = fm1_ms - vlast_ms, k1 = PE[pg * 4u], k2 = PE[pg * 4u + 1u], k3 = PE[pg * 4u + 2u], k4 = PE[pg * 4u + 3u], lv, lx;
    vlast_ms = fm1_ms;
    vkick = vis_kick;
    if (vnote) { int32_t dh = (((int32_t)((vnote * 5u) & 255u) - (int32_t)vhn + 128) & 255) - 128; vhn = (uint32_t)((int32_t)vhn + (dh >> 2)) & 255u; }   /* glide round the wheel to the held note's colour */
    dt = dt < 4u ? 4u : dt > 100u ? 100u : dt;
    lv = vis_pk >> 7;
    lv = lv > 255u ? 255u : lv;
    vlevel = (vlevel * 3u + lv) >> 2;
    vtime[pg][0] += dt * (4u + k3);
    vtime[pg][1] += dt * (6u + (k3 >> 1) + (vlevel >> 3) + (vkick >> 3));
    vtime[pg][2] += dt * 3u;
    vtime[pg][3] += dt * 2u + (vlevel >> 2);
    build_pal(pg == 4u || pg == 8u, (uint32_t)((int32_t)(k4 * 2u + (vtime[pg][3] >> 8) + vhn) + (vis_lfo >> 10)) & 255u);   /* COLOR, drift, held note, and the patch LFO swinging it */
    switch (pg) {
    case 0:                                     /* kaleidoscope: MIRROR copies, DEPTH rings, SPIN */
        V.a = 3 + (int32_t)(k1 * 13u >> 7);
        V.b = 2 + (int32_t)(k2 * 14u >> 7) + (int32_t)(vlevel >> 6) + (int32_t)(vkick >> 5);
        V.t1 = (vtime[0][0] >> 10) & 255u; V.t2 = (vtime[0][1] >> 8) & 255u; V.t3 = (vtime[0][2] >> 7) & 255u; V.t4 = (vtime[0][3] >> 6) & 255u;
        break;
    case 1:                                     /* tunnel: TWIST, RINGS, SPEED */
        V.a = (int32_t)k1 + (int32_t)(vkick >> 3); V.b = 2 + (int32_t)(k2 * 16u >> 7);
        V.t1 = (vtime[1][0] >> 10) & 255u; V.t2 = (vtime[1][1] >> 7) & 255u;
        break;
    case 2:                                     /* waves: ribbons of colour. FREQUENCY, HEIGHT, SPEED */
        V.a = 1 + (int32_t)(k1 * 4u >> 7); V.b = 5 + (int32_t)(k2 * 40u >> 7) + (int32_t)(vkick >> 4); V.c = 3 + (int32_t)(vlevel >> 6);
        V.t1 = (vtime[2][0] >> 8) & 255u; V.t2 = (vtime[2][1] >> 7) & 255u; V.t3 = (vtime[2][2] >> 5) & 255u;
        break;
    case 3:                                     /* plasma: SCALE, WARP, SPEED */
        V.a = 2 + (int32_t)(k1 * 8u >> 7); V.b = 1 + (int32_t)(k1 * 4u >> 7); V.c = (int32_t)(k2 * 24u >> 7) + (int32_t)(vlevel >> 5) + (int32_t)(vkick >> 4);
        V.t1 = (vtime[3][0] >> 7) & 255u; V.t2 = (vtime[3][1] >> 7) & 255u; V.t3 = (vtime[3][2] >> 5) & 255u; V.t4 = (vtime[3][3] >> 6) & 255u;
        break;
    case 4: melt_update(k1, k2, k3, k4, 4u); break;
    case 5: {                                   /* zone plate: two moving centres, RADIUS of their orbit, RINGS, SPEED */
        int32_t R = 10 + (int32_t)(k1 * 40u >> 7) + (int32_t)(vkick >> 4);
        uint32_t o = (vtime[5][0] >> 10) & 255u;
        V.cx1 = 60 + ((SIN8[o] * R) >> 7); V.cy1 = 60 + ((SIN8[(o + 64u) & 255u] * R) >> 7);
        V.cx2 = 60 - ((SIN8[(o * 2u + 40u) & 255u] * R) >> 7); V.cy2 = 60 - ((SIN8[(o * 2u + 104u) & 255u] * R) >> 7);
        V.a = 2 + (int32_t)((127u - k2) * 3u >> 7);
        V.t2 = (vtime[5][1] >> 6) & 255u; V.t3 = (vtime[5][2] >> 5) & 255u; V.t4 = (vtime[5][3] >> 6) & 255u;
        for (lx = 0; lx < VW; lx++) {
            int32_t d1 = (int32_t)lx - V.cx1, d2 = (int32_t)lx - V.cx2;
            zsq[0][lx] = (uint16_t)(d1 * d1); zsq[1][lx] = (uint16_t)(d2 * d2);
        }
        break; }
    case 6:                                     /* flower: PETALS, HEIGHT, SPIN */
        V.a = 3 + (int32_t)(k1 * 9u >> 7); V.b = 24 + (int32_t)(k2 * 90u >> 7) + (int32_t)(vlevel >> 3) + (int32_t)(vkick >> 2);
        V.t1 = (vtime[6][0] >> 10) & 255u; V.t2 = (vtime[6][1] >> 6) & 255u;
        break;
    case 7:                                     /* spiral galaxy: ARMS, TWIST, SPIN */
        V.a = 1 + (int32_t)(k1 * 5u >> 7); V.b = 2 + (int32_t)(k2 * 14u >> 7) + (int32_t)(vkick >> 6);
        V.t1 = (vtime[7][0] >> 10) & 255u; V.t2 = (vtime[7][1] >> 6) & 255u;
        break;
    case 8: warp_update(k1, k2, k3); break;
    default:                                    /* horizon: TILES, HEIGHT, SPEED */
        V.a = 3 + (int32_t)(k1 * 3u >> 7); V.b = 44 + (int32_t)(k2 * 30u >> 7);
        V.t1 = (vtime[9][1] >> 7) & 0xFFFFu; V.t2 = (vtime[9][2] >> 6) & 255u; V.t3 = (vtime[9][3] >> 6) & 255u;
        break;
    }
}

/* one cell row, two pixels wide each, into dst (240 pixels) */
static void vis_row(uint32_t pg, uint32_t ly, uint16_t *dst)
{
    const uint8_t *an = ANG + ly * VW, *ra = RAD + ly * VW;
    uint32_t lx, c;
    switch (pg) {
    case 0:
        for (lx = 0; lx < VW; lx++) {
            uint32_t a = ((uint32_t)an[lx] + V.t1) & 255u, a2 = (a * (uint32_t)V.a) & 255u, m = a2 < 128u ? a2 : 255u - a2, r = ra[lx];
            int32_t v1 = SIN8[((r * (uint32_t)V.b >> 2) + V.t2) & 255u], v2 = SIN8[((m << 1) + (r >> 1) + V.t3) & 255u],
                    v3 = SIN8[((m * 3u) ^ (r + V.t4)) & 255u];
            c = vpal[((uint32_t)((v1 + v2 + v3) >> 2) + 128u + (r >> 1)) & 255u];
            dst[2u * lx] = dst[2u * lx + 1u] = (uint16_t)c;
        }
        break;
    case 1:
        for (lx = 0; lx < VW; lx++) {
            uint32_t r = ra[lx], z = INVR[r], a = ((uint32_t)an[lx] + ((z * (uint32_t)V.a) >> 9) + V.t1) & 255u,
                     tz = ((z * (uint32_t)V.b) >> 4) + V.t2;
            int32_t v = SIN8[tz & 255u];
            c = vpal[(((a * 4u) ^ tz) + (uint32_t)(v >> 1)) & 255u];
            dst[2u * lx] = dst[2u * lx + 1u] = (uint16_t)c;
        }
        break;
    case 2:
        for (lx = 0; lx < VW; lx++) {
            int32_t w = ((SIN8[(lx * (uint32_t)V.a + V.t1) & 255u] * V.b) >> 7) + (SIN8[(lx * 5u + ly + V.t2) & 255u] >> 4), y = (int32_t)ly + w;
            c = vpal[((uint32_t)(y * V.c) + V.t3) & 255u];
            dst[2u * lx] = dst[2u * lx + 1u] = (uint16_t)c;
        }
        break;
    case 3: {
        int32_t y2 = SIN8[(ly * (uint32_t)V.a + V.t2) & 255u];
        for (lx = 0; lx < VW; lx++) {
            int32_t w = SIN8[((ly * 3u) + V.t3 + lx) & 255u], x1 = (int32_t)lx + ((w * V.c) >> 7), v;
            v = SIN8[((uint32_t)(x1 * V.a) + V.t1) & 255u] + y2 + SIN8[((uint32_t)((x1 + (int32_t)ly) * V.b) + V.t3) & 255u]
              + SIN8[(((uint32_t)ra[lx] * (uint32_t)V.b >> 2) + V.t4) & 255u];
            c = vpal[((uint32_t)(v >> 2) + 128u) & 255u];
            dst[2u * lx] = dst[2u * lx + 1u] = (uint16_t)c;
        }
        break; }
    case 4: {
        const uint8_t *s = vbuf[vcur] + ly * VW;
        for (lx = 0; lx < VW; lx++) { c = vpal[s[lx]]; dst[2u * lx] = dst[2u * lx + 1u] = (uint16_t)c; }
        break; }
    case 5: {
        int32_t dy1 = (int32_t)ly - V.cy1, dy2 = (int32_t)ly - V.cy2;
        uint32_t q1 = (uint32_t)(dy1 * dy1), q2 = (uint32_t)(dy2 * dy2), sh = (uint32_t)V.a;
        for (lx = 0; lx < VW; lx++) {
            int32_t v = SIN8[(((zsq[0][lx] + q1) >> sh) + V.t2) & 255u] + SIN8[(((zsq[1][lx] + q2) >> sh) - V.t3) & 255u];
            c = vpal[((uint32_t)(v >> 1) + 128u + V.t4) & 255u];
            dst[2u * lx] = dst[2u * lx + 1u] = (uint16_t)c;
        }
        break; }
    case 6:
        for (lx = 0; lx < VW; lx++) {
            uint32_t a = ((uint32_t)an[lx] + V.t1) & 255u;
            int32_t rr = (int32_t)ra[lx] + ((SIN8[(a * (uint32_t)V.a) & 255u] * V.b) >> 7);
            c = vpal[((uint32_t)(rr * 3) + V.t2) & 255u];
            dst[2u * lx] = dst[2u * lx + 1u] = (uint16_t)c;
        }
        break;
    case 7:
        for (lx = 0; lx < VW; lx++) {
            uint32_t a = ((uint32_t)an[lx] + V.t1) & 255u, r = ra[lx];
            c = vpal[((a * (uint32_t)V.a) + ((r * (uint32_t)V.b) >> 4) + V.t2) & 255u];
            dst[2u * lx] = dst[2u * lx + 1u] = (uint16_t)c;
        }
        break;
    case 8: {
        const uint8_t *s = vbuf[0] + (uint32_t)quant[ly] * VW;
        for (lx = 0; lx < VW; lx++) { c = vpal[s[quant[lx]]]; dst[2u * lx] = dst[2u * lx + 1u] = (uint16_t)c; }
        break; }
    default: {                                  /* horizon: a sun and a sky above, a chequered floor flying towards you below */
        int32_t hz = V.b;
        if ((int32_t)ly < hz) {
            int32_t sy = (int32_t)ly - (hz - 16);
            for (lx = 0; lx < VW; lx++) {
                int32_t sx = (int32_t)lx - 60;
                if (sx * sx + sy * sy < 18 * 18 && ((ly + (V.t1 >> 5)) & 7u) < 6u + (uint32_t)(sy > 0 ? 0 : 2))
                    c = vpal[(220u + ly * 2u + V.t3) & 255u];             /* the sun, cut by stripes */
                else
                    c = vpal[(40u + ly * 3u + V.t3) & 255u];              /* the sky */
                dst[2u * lx] = dst[2u * lx + 1u] = (uint16_t)c;
            }
        } else {
            uint32_t zr = ly - (uint32_t)hz + 1u, z = 2400u / zr, tsh = (uint32_t)V.a, sc = V.t1 + (vkick >> 1), shade = zr < 60u ? zr * 3u : 180u;
            for (lx = 0; lx < VW; lx++) {
                int32_t wx = ((int32_t)(lx - 60u) * (int32_t)z) >> 6;
                uint32_t chk = (((uint32_t)wx >> tsh) ^ ((z + sc) >> tsh)) & 1u;
                c = vpal[((chk ? 30u : 150u) + shade / 2u + V.t3) & 255u];
                dst[2u * lx] = dst[2u * lx + 1u] = (uint16_t)c;
            }
        }
        break; }
    }
}

/* ---- the heads-up display over the picture */
static const char *slab(uint32_t idx)           /* short names for the bottom of the picture */
{
    static const char *const S[NP] = {
        "VOICE", "", "", "",  "CUTOFF", "RESO", "ENV", "DRIVE",  "ATK", "DEC", "SUS", "REL",  "RATE", "DEPTH", "DEST", "SHAPE",
        "MIX", "TIME", "FDBK", "SPIRAL",  "REVERB", "SHIMR", "DAMP", "FREEZE",  "VOWEL", "TALK", "SHIFT", "SWEEP",
        "GLIDE", "TRIP", "SCALE", "ROOT",  "MODE", "OCTS", "RATE", "GATE",  "BPM", "SWING", "LEN", "XPOSE"};
    static const char *const O[4][3] = {{"PULSE", "DETUNE", "SUB"}, {"BUZZ", "MEEND", "DRONE"}, {"BAR", "LESLIE", "16FT"}, {"RATIO", "INDEX", "TINE"}};
    return idx > P_VOICE && idx < P_CUT ? O[ENG()][idx - 1u] : S[idx];
}

static void hud_top(uint32_t pg)                /* the page and patch (or the knob just turned), then the tempo and the transport */
{
    char s[24], bpm[8];
    int32_t x, rx;
    uint32_t j = 0;
    const char *arp = arp_cmd == 2u ? "HLD" : "ARP", *p;
    cv_rect(0, 0, 240, 16, BG);
    cv_rect(0, 1, 3, 14, hue565(vhn + 40u, 40u + (vkick * 215u >> 8)));   /* the left edge lights with every note */
    itoa3(60 + PE[P_BPM], bpm);
    rx = 236 - text_w(&FONT_S, bpm) - (arp_cmd ? text_w(&FONT_S, arp) + 8 : 0) - (playing || want_play ? 17 : 0)
        - (rec_arm ? text_w(&FONT_S, "REC") + 8 : 0);
    if (vtouch) {                               /* the knob, and what it is now: where the eye already is */
        uint32_t idx = pg * 4u + vtouch - 1u;
        p = slab(idx);
        while (*p) s[j++] = *p++;
        s[j++] = ' ';
        fmt_val(idx, s + j);
        cv_text(8, 0, &FONT_S, s, C_YE);
    } else if (vmsg) cv_text(8, 0, &FONT_S, vmsg, C_MG);
    else {
        x = cv_text(8, 0, &FONT_S, TAB[pg], C_CY) + 8;
        p = PNAME[cur];
        while (*p && j < 20u) s[j++] = *p++;
        if (edited) s[j++] = '*';
        s[j] = 0;
        if (x + text_w(&FONT_S, s) < rx) cv_text(x, 0, &FONT_S, s, C_PU);
    }
    x = cv_text(rx, 0, &FONT_S, bpm, C_TX) + 8;
    if (arp_cmd) x = cv_text(x, 0, &FONT_S, arp, C_CY) + 8;
    if (playing || want_play) x = cv_text(x, 0, &FONT_S, ">", C_CY) + 8;
    if (rec_arm) cv_text(x, 0, &FONT_S, "REC", C_MG);
}

static void hud_bottom(uint32_t pg)             /* the four knobs of the page, as rainbow segments (this is the last strip: rows 6..29) */
{
    uint32_t k;
    cv_rect(0, 6, 240, 24, BG);
    for (k = 0; k < 4u; k++) {
        int32_t x = (int32_t)k * 60 + 4;
        uint32_t hot = vtouch == k + 1u, v = PE[pg * 4u + k], i, lit = (v * 10u + 126u) / 127u;
        cv_text(x, 6, &FONT_S, slab(pg * 4u + k), hot ? C_YE : C_CY);
        for (i = 0; i < 10u; i++)
            cv_rect(x + (int32_t)i * 5, 24, 4, 5, i < lit ? (hot ? C_YE : hue565(k * 50u + 150u + i * 9u, 255u)) : C_GR);
    }
}

static void vis_step(uint32_t pg)               /* one strip of the picture: call it every pass of the main loop */
{
    uint32_t lr, ly0;
    if (vstrip == 0) vis_frame_begin(pg);
    lcd_sync();                                 /* the last strip may still be on its way out of cv_px */
    cv_w = 240; cv_h = 30;
    ly0 = vstrip * 15u;
    for (lr = 0; lr < 15u; lr++) {
        uint16_t *dst = cv_px + (lr * 2u) * 240u;
        vis_row(pg, ly0 + lr, dst);
        memcpy(dst + 240, dst, 480);
    }
    if (vstrip == 0) hud_top(pg);
    if (vstrip == 7) hud_bottom(pg);
    cv_blit(0, vstrip * 30u);
    vstrip = (vstrip + 1u) & 7u;
}
