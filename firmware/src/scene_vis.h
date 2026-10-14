/* SPDX-License-Identifier: GPL-3.0-only */
/* The five psychedelic screens (main loop). Included by tripdeck.c after the UI code.
 *
 * The picture is 120 x 120 cells, each drawn 2 x 2 pixels, sent as eight strips of 15 cell rows (240 x 30 pixels): one strip per
 * pass of the main loop, so the keys and knobs are read every ~10 ms while a frame is on its way to the screen.
 * Colours come from a palette rebuilt every frame (hue from COLOR); the pictures move with the sound: the output level pushes
 * the zoom, the speed and the sparks, and a held key puts a spark on the screen. */
#define VW 120u
#define VH 120u
static const char *const SCN[5] = {"KALEIDO", "TUNNEL", "PLASMA", "MELT", "WARP"};
static const char *const SCL[5][4] = {{"MIRROR", "DEPTH", "SPIN", "COLOR"}, {"TWIST", "RINGS", "SPEED", "COLOR"},
                                      {"SCALE", "WARP", "SPEED", "COLOR"}, {"ZOOM", "TRAIL", "SWIRL", "COLOR"},
                                      {"PIXEL", "DEPTH", "SPEED", "COLOR"}};

static uint8_t vbuf[2][VW * VH] __attribute__((section(".pool")));   /* melt and warp: the picture so far */
static uint16_t vpal[256];                      /* the palette, already byte swapped for the screen */
static uint32_t vcur, vstrip, vlast_ms, vlevel, vnote, vrnd = 0x2545F491u, vtime[5][4];
static uint32_t vtouch_ms[4], vtouch;           /* when each knob was last turned */
static const char *vmsg;                        /* a short message for the top line */
static uint8_t quant[VW];                       /* warp: the pixel size as a lookup */
static uint8_t sang[96];
static uint16_t srad[96];
static struct { int32_t a, b, c; uint32_t t1, t2, t3, t4; } V;

static uint32_t vrand(void) { vrnd ^= vrnd << 13; vrnd ^= vrnd >> 17; vrnd ^= vrnd << 5; return vrnd; }

static uint32_t scurve(uint32_t c) { return c < 128u ? (c * c) >> 7 : 255u - (((255u - c) * (255u - c)) >> 7); }

static void build_pal(uint32_t fade, uint32_t hue)  /* fade: index 0 is black and the brightness grows with the index */
{
    uint32_t i;
    for (i = 0; i < 256u; i++) {
        uint32_t ph = fade ? i * 2u + hue : i + hue, r, g, b, br;
        r = scurve((uint32_t)(128 + SIN8[ph & 255u]));
        g = scurve((uint32_t)(128 + SIN8[(ph + 85u) & 255u]));
        b = scurve((uint32_t)(128 + SIN8[(ph + 170u) & 255u]));
        if (fade) { br = i ? 64u + (i * 191u >> 8) : 0u; r = r * br >> 8; g = g * br >> 8; b = b * br >> 8; }   /* any trail stays visible until it is gone */
        vpal[i] = swap16(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    }
}

static void vis_init(void)
{
    uint32_t i;
    for (i = 0; i < 96u; i++) { sang[i] = (uint8_t)vrand(); srad[i] = (uint16_t)(16u + vrand() % 1300u); }
}

/* ---- melt: video feedback. The last picture is zoomed and turned a little, faded, and some sparks are added. */
static void melt_update(uint32_t k1, uint32_t k2, uint32_t k3, uint32_t k4)
{
    const uint8_t *src = vbuf[vcur];
    uint8_t *dst = vbuf[vcur ^ 1u];
    int32_t z = 256 + (((int32_t)k1 - 64) >> 3), rt = ((int32_t)k3 - 64) >> 2, ly, lx, i, n;
    uint32_t fade = 1u + ((127u - k2) >> 4), a0 = vtime[3][0] >> 8, val;
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
        int32_t R = i == 3 ? 30 : 10 + ((SIN8[((vtime[3][1] >> 8) + (uint32_t)i * 40u) & 255u] + 127) >> 3) + (int32_t)(vlevel >> 4);
        int32_t cx = 60 + ((SIN8[(ang + 64u) & 255u] * R) >> 7), cy = 60 + ((SIN8[ang & 255u] * R) >> 7);
        int32_t rr = 3 + (int32_t)(vlevel >> 6) + (i == 3 ? 2 : 0), dx, dy;
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
    uint32_t i, spd = 2u + (k3 >> 2) + (vlevel >> 5), fade = 2u + ((127u - k2) >> 4), blk = 1u + (k1 * 9u >> 7);
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
static void vis_frame_begin(uint32_t sc)
{
    uint32_t s = sc - 1u, dt = fm1_ms - vlast_ms, k1 = SP[s][0], k2 = SP[s][1], k3 = SP[s][2], k4 = SP[s][3], lv;
    vlast_ms = fm1_ms;
    dt = dt < 4u ? 4u : dt > 100u ? 100u : dt;
    lv = vis_pk >> 7;
    lv = lv > 255u ? 255u : lv;
    vlevel = (vlevel * 3u + lv) >> 2;
    vtime[s][0] += dt * (4u + k3);
    vtime[s][1] += dt * (6u + (k3 >> 1) + (vlevel >> 3));
    vtime[s][2] += dt * 3u;
    vtime[s][3] += dt * 2u + (vlevel >> 2);
    build_pal(sc >= 4u, (k4 * 2u + (vtime[s][3] >> 8)) & 255u);
    switch (sc) {
    case 1:                                     /* kaleidoscope: MIRROR copies, DEPTH rings, SPIN */
        V.a = 3 + (int32_t)(k1 * 13u >> 7);
        V.b = 2 + (int32_t)(k2 * 14u >> 7) + (int32_t)(vlevel >> 6);
        V.t1 = (vtime[0][0] >> 10) & 255u; V.t2 = (vtime[0][1] >> 8) & 255u; V.t3 = (vtime[0][2] >> 7) & 255u; V.t4 = (vtime[0][3] >> 6) & 255u;
        break;
    case 2:                                     /* tunnel: TWIST, RINGS, SPEED */
        V.a = (int32_t)k1; V.b = 2 + (int32_t)(k2 * 16u >> 7);
        V.t1 = (vtime[1][0] >> 10) & 255u; V.t2 = (vtime[1][1] >> 7) & 255u;
        break;
    case 3:                                     /* plasma: SCALE, WARP, SPEED */
        V.a = 2 + (int32_t)(k1 * 8u >> 7); V.b = 1 + (int32_t)(k1 * 4u >> 7); V.c = (int32_t)(k2 * 24u >> 7) + (int32_t)(vlevel >> 5);
        V.t1 = (vtime[2][0] >> 7) & 255u; V.t2 = (vtime[2][1] >> 7) & 255u; V.t3 = (vtime[2][2] >> 5) & 255u; V.t4 = (vtime[2][3] >> 6) & 255u;
        break;
    case 4: melt_update(k1, k2, k3, k4); break;
    default: warp_update(k1, k2, k3); break;
    }
}

/* one cell row, two pixels wide each, into dst (240 pixels) */
static void vis_row(uint32_t sc, uint32_t ly, uint16_t *dst)
{
    const uint8_t *an = ANG + ly * VW, *ra = RAD + ly * VW;
    uint32_t lx, c;
    switch (sc) {
    case 1:
        for (lx = 0; lx < VW; lx++) {
            uint32_t a = ((uint32_t)an[lx] + V.t1) & 255u, a2 = (a * (uint32_t)V.a) & 255u, m = a2 < 128u ? a2 : 255u - a2, r = ra[lx];
            int32_t v1 = SIN8[((r * (uint32_t)V.b >> 2) + V.t2) & 255u], v2 = SIN8[((m << 1) + (r >> 1) + V.t3) & 255u],
                    v3 = SIN8[((m * 3u) ^ (r + V.t4)) & 255u];
            c = vpal[((uint32_t)((v1 + v2 + v3) >> 2) + 128u + (r >> 1)) & 255u];
            dst[2u * lx] = dst[2u * lx + 1u] = (uint16_t)c;
        }
        break;
    case 2:
        for (lx = 0; lx < VW; lx++) {
            uint32_t r = ra[lx], z = INVR[r], a = ((uint32_t)an[lx] + ((z * (uint32_t)V.a) >> 9) + V.t1) & 255u,
                     tz = ((z * (uint32_t)V.b) >> 4) + V.t2;
            int32_t v = SIN8[tz & 255u];
            c = vpal[(((a * 4u) ^ tz) + (uint32_t)(v >> 1)) & 255u];
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
    default: {
        const uint8_t *s = vbuf[0] + (uint32_t)quant[ly] * VW;
        for (lx = 0; lx < VW; lx++) { c = vpal[s[quant[lx]]]; dst[2u * lx] = dst[2u * lx + 1u] = (uint16_t)c; }
        break; }
    }
}

static void hud_top(uint32_t sc)                /* the name (or the knob just turned), then the tempo and the transport */
{
    char s[24], n[8];
    int32_t x;
    uint32_t k, j = 0;
    cv_rect(0, 0, 240, 16, BG);
    if (vtouch) {                               /* the knob, and what it is now: where the eye already is */
        const char *p = SCL[sc - 1u][vtouch - 1u];
        while (*p) s[j++] = *p++;
        s[j++] = ' ';
        itoa3(SP[sc - 1u][vtouch - 1u], n);
        for (k = 0; n[k]; k++) s[j++] = n[k];
        s[j] = 0;
        cv_text(4, 0, &FONT_S, s, C_YE);
    } else if (vmsg) cv_text(4, 0, &FONT_S, vmsg, C_MG);
    else {
        x = cv_text(4, 0, &FONT_S, SCN[sc - 1u], C_CY) + 8;
        itoa3((int32_t)cpu_pct, n);
        s[0] = 'C'; s[1] = 'P'; s[2] = 'U';
        for (k = 0; n[k]; k++) s[3u + k] = n[k];
        s[3u + k] = '%'; s[4u + k] = 0;
        cv_text(x, 0, &FONT_S, s, C_PU);
    }
    {
        char bpm[8];
        const char *arp = arp_cmd == 2u ? "HLD" : "ARP";
        itoa3(60 + P[P_BPM], bpm);
        x = 236 - text_w(&FONT_S, bpm) - (arp_cmd ? text_w(&FONT_S, arp) + 8 : 0) - (playing || want_play ? 17 : 0)
            - (rec_arm ? text_w(&FONT_S, "REC") + 8 : 0);
        x = cv_text(x, 0, &FONT_S, bpm, C_TX) + 8;
        if (arp_cmd) x = cv_text(x, 0, &FONT_S, arp, C_CY) + 8;
        if (playing || want_play) x = cv_text(x, 0, &FONT_S, ">", C_CY) + 8;
        if (rec_arm) cv_text(x, 0, &FONT_S, "REC", C_MG);
    }
}

static void hud_bottom(uint32_t sc)             /* the four knobs of the scene, as bars (this is the last strip: rows 6..29) */
{
    uint32_t k;
    cv_rect(0, 6, 240, 24, BG);
    for (k = 0; k < 4u; k++) {
        int32_t x = (int32_t)k * 60 + 4;
        uint32_t hot = vtouch == k + 1u, v = SP[sc - 1u][k];
        cv_text(x, 6, &FONT_S, SCL[sc - 1u][k], hot ? C_YE : C_CY);
        cv_rect(x, 24, 52, 4, C_PU);
        cv_rect(x, 24, (int32_t)(v * 52u / 127u), 4, hot ? C_YE : C_MG);
    }
}

static void vis_step(uint32_t sc)               /* one strip of the picture: call it every pass of the main loop */
{
    uint32_t lr, ly0;
    if (vstrip == 0) vis_frame_begin(sc);
    lcd_sync();                                 /* the last strip may still be on its way out of cv_px */
    cv_w = 240; cv_h = 30;
    ly0 = vstrip * 15u;
    for (lr = 0; lr < 15u; lr++) {
        uint16_t *dst = cv_px + (lr * 2u) * 240u;
        vis_row(sc, ly0 + lr, dst);
        memcpy(dst + 240, dst, 480);
    }
    if (vstrip == 0) hud_top(sc);
    if (vstrip == 7) hud_bottom(sc);
    cv_blit(0, vstrip * 30u);
    vstrip = (vstrip + 1u) & 7u;
}
