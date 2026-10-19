/* SPDX-License-Identifier: GPL-3.0-only */
/* A fake FM-1 on a computer: the app (firmware/src/tripdeck.c) runs unchanged on it. platform.c includes
 * this file instead of the hardware when DINGHY_HOST is set. A script presses keys over time; out come
 * the screen (PPM, at the times SHOT asks) and the sound (WAV, as the FM-1 sends it over USB).
 *
 *   mkdir -p build/host
 *   cc -O2 -Wall -DDINGHY_HOST -Ifirmware/src -Ifirmware/gen -o build/host/tripdeck firmware/src/tripdeck.c
 *   build/host/tripdeck build/host/tripdeck        (-> build/host/tripdeck.wav, build/host/tripdeck-<ms>.ppm)
 *   build/host/tripdeck OUT script.txt [FPS]    (your own script; with FPS: OUT-NNNNN.ppm, a frame every 1/FPS s:
 *                                             a film of your firmware, e.g. ffmpeg -r 30 -i OUT-%05d.ppm ...)
 *
 * Change SCRIPT below to try your own app. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef APP_NAME
#define APP_NAME "TRIPDECK"
#endif
#ifndef APP_VER
#define APP_VER "0.0"
#endif
#define FS 44100u
#define HALF_FRAMES 256u

static volatile uint32_t fm1_ms;
static struct { uint32_t notes, buttons; } fm1_in;
static uint32_t host_edges, host_rel, host_rel_at[16];
static uint32_t fm1_input_edges(uint32_t *released) { uint32_t b = host_edges; host_edges = 0; *released = host_rel; host_rel = 0; return b; }
static uint32_t fm1_input_note_edges(void) { return 0; }   /* (the script sets fm1_in.notes) */
static int32_t host_enc[8];
static int32_t fm1_enc_take(uint32_t e) { int32_t v = host_enc[e & 7u]; host_enc[e & 7u] = 0; return v; }
#define FM1_TICKS_PER_US 24u
static uint32_t fm1_ticks(void) { return 0; }   /* (no cycle counter on a computer: CPU reads 0) */
static void fm1_led_key(uint32_t id, int on) { (void)id; (void)on; }

/* the screen: 240 x 240 RGB565, as the ST7789 gets it (gfx.c sends the bytes swapped) */
static uint16_t fb[240 * 240];
static void lcd_sync(void) {}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    for (j = y; j < y + h && j < 240u; j++)
        for (i = x; i < x + w && i < 240u; i++)
            fb[j * 240u + i] = c;
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (x + i < 240u && y + j < 240u) {
                uint16_t c = px[j * w + i];
                fb[(y + j) * 240u + x + i] = (uint16_t)(c >> 8 | c << 8);
            }
}
#define CV_MAX (240u * 64u)
#include "gfx.c"

static uint8_t host_store[256 + 4];
static int settings_load(void *p, uint32_t n)
{
    if (host_store[0] != 1 || n > 256u) return -1;
    memcpy(p, host_store + 4, n);
    return 0;
}
static int settings_save(const void *p, uint32_t n)
{
    host_store[0] = 1;
    memcpy(host_store + 4, p, n);
    printf("%6u ms  settings saved (%u B)\n", fm1_ms, n);
    return 0;
}

static uint8_t mq[64][3];
static uint32_t mq_w, mq_r;
static int midi_get(uint8_t *st, uint8_t *d1, uint8_t *d2)
{
    if (mq_r == mq_w) return 0;
    *st = mq[mq_r % 64u][0]; *d1 = mq[mq_r % 64u][1]; *d2 = mq[mq_r % 64u][2];
    mq_r++;
    return 1;
}
static void midi_send(uint32_t st, uint32_t d1, uint32_t d2) { printf("%6u ms  MIDI out %02X %u %u\n", fm1_ms, st, d1, d2); }

/* the TRS jack: the parser of firmware/src/midi_uart.c, fed with bytes by "T uart B0 B1 ..." (it queues into the same ring) */
static int um_put(uint32_t pkt)
{
    if (mq_w - mq_r >= 64u) return 0;
    mq[mq_w % 64u][0] = (uint8_t)(pkt >> 8); mq[mq_w % 64u][1] = (uint8_t)(pkt >> 16); mq[mq_w % 64u][2] = (uint8_t)(pkt >> 24);
    mq_w++;
    return 1;
}
#include "../firmware/src/midi_uart.c"

static void app_audio(int32_t *out, uint32_t n);
static void app_main(void);

/* the script: at t ms, keys down (bit k = key k, 0 = F3), a button edge, a MIDI message. A script file (the second
 * argument) replaces it, a line each: "T keys MASK" (hex or decimal), "T btn ID [MS]", "T enc ID N", "T shot", "T midi ST D1 D2", "T uart B0 B1 ...", "T end" */
typedef struct { uint32_t t, keys; int btn; uint8_t st, d1, d2; } host_ev_t;
static const host_ev_t SCRIPT0[] = {
    {200, 1u << 7, -1, 0, 0, 0},                                /* C4 */
    {700, 1u << 7 | 1u << 11 | 1u << 14, -1, 0, 0, 0},          /* + E4 G4: a chord */
    {1500, 0, -1, 0, 0, 0},
    {1700, 0, -1, 0x90, 81, 120},                               /* MIDI in: A5 */
    {2300, 0, -1, 0x80, 81, 0},
    {2500, 0, BTN_OCT_UP, 0, 0, 0},
    {2700, 1u << 2 | 1u << 9, -1, 0, 0, 0},                     /* G4 + D5, an octave up */
    {3400, 0, -1, 0, 0, 0},
};
static const host_ev_t *script = SCRIPT0;
static uint32_t script_n = sizeof SCRIPT0 / sizeof SCRIPT0[0], end_ms = 6500, frame_fps;
static uint32_t SHOT[16] = {1000, 3000}, shot_n = 2;

static int shot_script;
static void (*host_param_cb)(uint32_t, uint32_t);   /* the app sets this: "T param IDX VAL" sets a parameter (see tripdeck.c) */
static uint32_t host_splash_ms;                 /* "T splash": run the loading screen for T ms before the app starts */
static void read_script(const char *path)
{
    static host_ev_t ev[4096];
    char line[256], what[16];
    FILE *f = fopen(path, "r");
    uint32_t n = 0;
    if (!f) { perror(path); exit(1); }
    while (fgets(line, sizeof line, f) && n < 4096u) {
        host_ev_t e = {0, 0, -1, 0, 0, 0};
        unsigned a = 0, b = 0, c = 0;
        if (line[0] == '#' || sscanf(line, "%u %15s", &e.t, what) != 2) continue;
        if (!strcmp(what, "end")) { end_ms = e.t; continue; }
        if (!strcmp(what, "splash")) { host_splash_ms = e.t; continue; }
        if (!strcmp(what, "shot")) { if (!shot_script) { shot_n = 0; shot_script = 1; } if (shot_n < 16u) SHOT[shot_n++] = e.t; continue; }
        if (!strcmp(what, "keys")) { sscanf(line, "%*u %*s %i", (int *)&e.keys); }
        else if (!strcmp(what, "enc")) { int id = 0, dn = 0; sscanf(line, "%*u %*s %d %d", &id, &dn); e.btn = 100 + id; e.keys = (uint32_t)dn; }
        else if (!strcmp(what, "btn")) { int dur = 0; sscanf(line, "%*u %*s %d %d", &e.btn, &dur); e.keys = (uint32_t)dur; }
        else if (!strcmp(what, "param")) { int id = 0, val = 0; sscanf(line, "%*u %*s %d %d", &id, &val); e.btn = 200 + id; e.keys = (uint32_t)val; }
        else if (!strcmp(what, "uart")) {         /* raw bytes (hex) on the TRS jack: one event per byte */
            char *q = strstr(line, "uart") + 4;
            for (;;) {
                char *end;
                long v = strtol(q, &end, 16);
                if (end == q) break;
                q = end;
                if (n < 4096u) { host_ev_t u = e; u.btn = 300; u.keys = (uint32_t)(v & 0xFF); ev[n++] = u; }
            }
            continue;
        }
        else if (!strcmp(what, "midi")) { sscanf(line, "%*u %*s %i %u %u", (int *)&a, &b, &c); e.st = (uint8_t)a; e.d1 = (uint8_t)b; e.d2 = (uint8_t)c; }
        else continue;
        ev[n++] = e;
    }
    fclose(f);
    script = ev;
    script_n = n;
}

static const char *host_out = "build/host/tripdeck";
static int16_t *wav;
static uint32_t wav_n;

static void write_ppm_as(const char *name)
{
    FILE *f;
    uint32_t i;
    if (!(f = fopen(name, "wb"))) { perror(name); exit(1); }
    fprintf(f, "P6 240 240 255\n");
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t c = fb[i];
        fputc((c >> 11) * 255 / 31, f);
        fputc((c >> 5 & 63) * 255 / 63, f);
        fputc((c & 31) * 255 / 31, f);
    }
    fclose(f);
}
static void write_ppm(uint32_t t)
{
    char name[512];
    snprintf(name, sizeof name, "%s-%u.ppm", host_out, t);
    write_ppm_as(name);
    printf("%6u ms  screen -> %s\n", t, name);
}

static void write_wav(void)
{
    char name[512];
    FILE *f;
    uint32_t bytes = wav_n * 4u, v[] = {16, 0x00020001u, FS, FS * 4u, 0x00100004u};
    snprintf(name, sizeof name, "%s.wav", host_out);
    if (!(f = fopen(name, "wb"))) { perror(name); exit(1); }
    fwrite("RIFF", 1, 4, f); bytes += 36; fwrite(&bytes, 4, 1, f); bytes -= 36;
    fwrite("WAVEfmt ", 1, 8, f); fwrite(v, 4, 5, f);
    fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
    fwrite(wav, 4, wav_n, f);
    fclose(f);
    printf("sound  -> %s (%u frames)\n", name, wav_n);
}

static void platform_poll(void)                 /* one call = one millisecond */
{
    static uint32_t step, shot, owed, frame;
    static int32_t blk[2u * HALF_FRAMES];
    fm1_ms++;
    for (uint32_t bt = 0; bt < 16u; bt++)
        if (host_rel_at[bt] && fm1_ms >= host_rel_at[bt]) { host_rel_at[bt] = 0; fm1_in.buttons &= ~(1u << bt); host_rel |= 1u << bt; }
    while (step < script_n && script[step].t <= fm1_ms) {
        if (script[step].st) {
            mq[mq_w % 64u][0] = script[step].st; mq[mq_w % 64u][1] = script[step].d1; mq[mq_w % 64u][2] = script[step].d2;
            mq_w++;
        } else if (script[step].btn >= 300)
            um_byte(script[step].keys);
        else if (script[step].btn >= 200) {
            if (host_param_cb) host_param_cb((uint32_t)script[step].btn - 200u, script[step].keys);
        } else if (script[step].btn >= 100)
            host_enc[(script[step].btn - 100) & 7] += (int32_t)script[step].keys;
        else if (script[step].btn >= 0)
        {                                       /* a press now (fm1_in.buttons), its release after keys ms (default 80) */
            host_edges |= 1u << script[step].btn;
            fm1_in.buttons |= 1u << script[step].btn;
            host_rel_at[script[step].btn] = fm1_ms + (script[step].keys ? script[step].keys : 80u);
        }
        else
            fm1_in.notes = script[step].keys;
        step++;
    }
    owed += FS;                                 /* the frames this millisecond is worth, in blocks as the ISR renders */
    while (owed >= 1000u * HALF_FRAMES) {
        uint32_t i;
        owed -= 1000u * HALF_FRAMES;
        memset(blk, 0, sizeof blk);
        app_audio(blk, HALF_FRAMES);
        wav = realloc(wav, (wav_n + HALF_FRAMES) * 4u);
        for (i = 0; i < 2u * HALF_FRAMES; i++) {
            int32_t v = blk[i];                 /* (USB audio at the MASTER knob all the way up) */
            wav[2u * wav_n + i] = (int16_t)(v > 32767 ? 32767 : v < -32767 ? -32767 : v);
        }
        wav_n += HALF_FRAMES;
    }
    if (frame_fps) {                            /* a film: a frame every 1/fps s, as the screen is then */
        if ((uint64_t)frame * 1000u <= (uint64_t)fm1_ms * frame_fps) {
            char name[512];
            snprintf(name, sizeof name, "%s-%05u.ppm", host_out, frame++);
            write_ppm_as(name);
        }
    } else if (shot < shot_n && fm1_ms == SHOT[shot])
        write_ppm(SHOT[shot++]);
    if (fm1_ms >= end_ms) {
        write_wav();
        exit(0);
    }
}

int main(int argc, char **argv)
{
    if (argc > 1) host_out = argv[1];
    if (argc > 2) read_script(argv[2]);
    if (argc > 3) frame_fps = (uint32_t)atoi(argv[3]);
    app_main();
    return 0;
}
