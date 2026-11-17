/* SPDX-License-Identifier: GPL-3.0-only */
/* FM1 DECK HAL adapter for SHRUB (decompiled from Shrub.vst3 / shrub.wasm via tools/shrub_wasm2c.py). */
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "shrub_wasm_gen.h"
#include "../../../app/src/main/cpp/mk3/font8x8.h"

#define FS 44100u

enum {
    B_FX, B_SCL, B_ENV, B_LFO, B_EDIT, B_GLO, B_HOME, B_SAVE,
    B_ARP, B_SEQ, B_PLAY, B_REC, B_OCTDN, B_OCTUP, NB
};
enum { EN_SELECT, EN_ALGO, EN_PRESET, EN_K1, EN_K2, EN_K3, EN_K4, NE };

static const char *const B_NAME[NB] = {
    "FX", "SCL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE",
    "ARP", "SEQ", "PLAY", "REC", "OCT-", "OCT+"
};

/* ------------------------------------------------------------ persistent NOR flash --- */
#define SHRUB_NOR_MAGIC   0x53485242u /* "SHRB" */
#define SHRUB_NOR_VERSION 1u
static uint8_t nor[0x100000];
static uint32_t shrub_flash_writes;

static void nor_wr32(uint32_t off, uint32_t v)
{
    nor[off + 0u] = (uint8_t)(v & 0xFFu);
    nor[off + 1u] = (uint8_t)((v >> 8) & 0xFFu);
    nor[off + 2u] = (uint8_t)((v >> 16) & 0xFFu);
    nor[off + 3u] = (uint8_t)((v >> 24) & 0xFFu);
}

static uint32_t nor_rd32(uint32_t off)
{
    return (uint32_t)nor[off + 0u]
        | ((uint32_t)nor[off + 1u] << 8)
        | ((uint32_t)nor[off + 2u] << 16)
        | ((uint32_t)nor[off + 3u] << 24);
}

/* ------------------------------------------------------------ SHRUB pages & mapping --- */
typedef struct {
    const char *title;
    uint16_t params[4];
} ShrubPageDef;

typedef struct {
    const char *group_name;
    uint8_t btn;
    uint8_t layer_id;
    uint8_t sub_count;
    ShrubPageDef subs[4];
} ShrubGroupDef;

/* Page table matching params.js indices */
static const ShrubGroupDef SHRUB_GROUPS[9] = {
    { "OSC A", B_HOME, 0u /* NONE */, 2u, {
        { "OSC A",     {   0u,   1u,   2u,   3u } }, /* TABLE, WT POS, WARP, AMOUNT */
        { "OSC A UNI", {   5u,   6u,   9u,   4u } }, /* UNISON, DETUNE, OCT, LEVEL */
    }},
    { "OSC B", B_EDIT, 4u /* EDIT */, 2u, {
        { "OSC B",     {  13u,  14u,  15u,  16u } }, /* TABLE, WT POS, WARP, AMOUNT */
        { "OSC B/SUB", {  18u,  19u,  26u,  28u } }, /* UNISON, DETUNE, SUB SHAPE, SUB LVL */
    }},
    { "FILTER", B_SCL, 3u /* SCL */, 2u, {
        { "FILTER",    {  31u,  32u,  33u,  38u } }, /* MODE, CUTOFF, RES, ENV2->CUT */
        { "FLT DRIVE", {  34u,  35u,  37u,  36u } }, /* DRIVE, FAT, KEYTRK, MIX */
    }},
    { "ENV", B_ENV, 11u /* MENU/ENV */, 3u, {
        { "ENV 1 AMP", {  39u,  41u,  42u,  43u } }, /* A, D, S, R */
        { "ENV 2 FLT", {  47u,  49u,  50u,  51u } }, /* A, D, S, R */
        { "ENV 3 MOD", { 322u, 324u, 325u, 326u } }, /* A, D, S, R */
    }},
    { "LFO", B_LFO, 10u /* LFO */, 4u, {
        { "LFO 1",     {  55u,  57u,  58u, 340u } }, /* RATE, MODE, SKEW, DEPTH */
        { "LFO 2",     {  60u,  62u,  63u, 341u } },
        { "LFO 3",     { 242u, 244u, 245u, 342u } },
        { "LFO 4",     { 247u, 249u, 250u, 343u } },
    }},
    { "FX", B_FX, 1u /* FX */, 4u, {
        { "FX OTT",    { 124u, 127u, 128u, 129u } }, /* DEPTH, TIME, GAIN, MIX */
        { "FX DIST",   {  98u,  99u, 103u, 100u } }, /* MODE, DRIVE, FREQ, MIX */
        { "FX CH/DLY", { 110u, 115u, 117u, 118u } }, /* CHO MIX, DLY L, FB, DLY MIX */
        { "FX REVERB", { 130u, 131u, 132u, 134u } }, /* MODE, SIZE, DECAY, MIX */
    }},
    { "GLO", B_GLO, 2u /* GLO */, 2u, {
        { "MACROS",    { 312u, 313u, 314u, 315u } }, /* M1..M4 */
        { "GLOBAL",    {  91u,  93u,  96u,  97u } }, /* MONO, GLIDE, KEY, SCALE */
    }},
    { "ARP", B_ARP, 9u /* ARP */, 1u, {
        { "ARP",       { 138u, 139u, 140u, 141u } }, /* MODE, RATE, OCTAVES, GATE */
    }},
    { "SEQ", B_SEQ, 5u /* SEQ */, 2u, {
        { "SEQ/NOISE", {  90u, 165u,  29u,  30u } }, /* TEMPO, VOICES, N TYPE, N LVL */
        { "CHAOS/M1",  { 308u, 310u,  65u,  67u } }, /* CH1 RATE, CH2 RATE, M1 SRC, M1 AMT */
    }},
};

static const char *const SHRUB_CATEGORIES[7] = {
    "BASS", "LEAD", "MISC", "PAD", "PLUCK", "SEQ", "SYNTH"
};

/* ------------------------------------------------------------ device state --- */
static uint32_t shrub_booted;
static uint32_t shrub_seed_val;
static uint64_t shrub_samples;
static uint32_t shrub_ms;
static uint32_t shrub_last_ui_ms;
static uint32_t shrub_splash_active;

static uint32_t shrub_btn_mask;
static uint32_t shrub_key_mask;
static int32_t  shrub_octave; /* -3 .. +3 */
static uint8_t  shrub_note_on[27];
static int8_t   shrub_note_midi[27];

static uint32_t shrub_cur_group;
static uint32_t shrub_cur_sub[9];
static uint32_t shrub_preset_idx;
static uint32_t shrub_playing;
static uint32_t shrub_recording;

static uint32_t shrub_master_adc = 700u;
static float    shrub_master_smooth = 0.468f;

static uint32_t shrub_saved_gen;
static uint32_t shrub_saved_seq_ver;
static uint32_t shrub_saved_preset;
static uint32_t shrub_last_dirty_ms;
static uint32_t shrub_save_led_until_ms;

static uint16_t web_fb[240u * 240u];
static uint32_t web_draws;
static float out_l[1024], out_r[1024];

/* ------------------------------------------------------------ 240x240 RGB565 drawing --- */
/* LCD_WIRE: big-endian RGB565 in memory (high byte first) */
static inline uint16_t rgb565_be(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t c = (uint16_t)(((uint16_t)(r >> 3) << 11) | ((uint16_t)(g >> 2) << 5) | ((uint16_t)(b >> 3)));
    return (uint16_t)((c >> 8) | (c << 8));
}

static void fb_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c_be)
{
    for (uint32_t j = 0; j < h && (y + j) < 240u; j++) {
        uint16_t *row = &web_fb[(y + j) * 240u + x];
        for (uint32_t i = 0; i < w && (x + i) < 240u; i++) {
            row[i] = c_be;
        }
    }
}

static void fb_rect_outline(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c_be)
{
    if (w < 2u || h < 2u) return;
    fb_fill(x, y, w, 1u, c_be);
    fb_fill(x, y + h - 1u, w, 1u, c_be);
    fb_fill(x, y, 1u, h, c_be);
    fb_fill(x + w - 1u, y, 1u, h, c_be);
}

static void fb_draw_char(uint32_t x, uint32_t y, char ch, uint16_t fg_be, uint32_t scale)
{
    uint8_t uc = (uint8_t)ch;
    if (uc >= 128u) uc = '?';
    const uint8_t *glyph = font8x8_basic[uc];
    for (uint32_t r = 0; r < 8u; r++) {
        uint8_t bits = glyph[r];
        if (!bits) continue;
        for (uint32_t c = 0; c < 8u; c++) {
            /* font8x8_basic stores leftmost pixel in MSB (bit 7) */
            if (bits & (0x80u >> c)) {
                fb_fill(x + c * scale, y + r * scale, scale, scale, fg_be);
            }
        }
    }
}

static void fb_draw_text(uint32_t x, uint32_t y, const char *s, uint16_t fg_be, uint32_t scale, uint32_t max_chars)
{
    if (!s) return;
    uint32_t adv = 8u * scale;
    for (uint32_t i = 0; s[i] && i < max_chars; i++) {
        if (x + adv > 240u) break;
        fb_draw_char(x, y, s[i], fg_be, scale);
        x += adv;
    }
}

static const char *wasm_cstr(uint32_t ptr)
{
    if (ptr == 0u || ptr >= SHRUB_WASM_MEM_BYTES) return "";
    return (const char *)(wasm_mem + ptr);
}

static void shrub_draw_dashboard(void)
{
    const uint16_t c_bg      = rgb565_be(17, 19, 22);
    const uint16_t c_hdr     = rgb565_be(28, 33, 39);
    const uint16_t c_card    = rgb565_be(24, 28, 33);
    const uint16_t c_border  = rgb565_be(48, 56, 66);
    const uint16_t c_green   = rgb565_be(155, 238, 94);
    const uint16_t c_green2  = rgb565_be(72, 180, 110);
    const uint16_t c_dim     = rgb565_be(140, 152, 165);
    const uint16_t c_white   = rgb565_be(232, 238, 244);
    const uint16_t c_amber   = rgb565_be(255, 168, 64);
    const uint16_t c_bar_bg  = rgb565_be(12, 14, 17);

    fb_fill(0u, 0u, 240u, 240u, c_bg);

    /* Top bar: SHRUB badge + category + preset number */
    fb_fill(0u, 0u, 240u, 42u, c_hdr);
    fb_draw_text(6u, 5u, "SHRUB", c_green, 1u, 8u);

    uint32_t cat = sw_preset_category(shrub_preset_idx);
    const char *cat_str = (cat < 7u) ? SHRUB_CATEGORIES[cat] : "SYNTH";
    char hdr_right[20];
    uint32_t pnum = shrub_preset_idx + 1u;
    hdr_right[0] = (char)('0' + (pnum / 100u) % 10u);
    hdr_right[1] = (char)('0' + (pnum / 10u) % 10u);
    hdr_right[2] = (char)('0' + (pnum % 10u));
    hdr_right[3] = ' ';
    uint32_t hi = 4u;
    for (uint32_t k = 0; cat_str[k] && hi < 18u; k++) hdr_right[hi++] = cat_str[k];
    hdr_right[hi] = 0;
    fb_draw_text(138u, 5u, hdr_right, c_dim, 1u, 12u);

    /* Preset name (2x or 1x depending on length) */
    const char *pname = wasm_cstr(sw_preset_name(shrub_preset_idx));
    if (strlen(pname) <= 13u) {
        fb_draw_text(6u, 20u, pname, c_white, 2u, 14u);
    } else {
        fb_draw_text(6u, 24u, pname, c_white, 1u, 28u);
    }

    /* Wavetable scope box (y = 46 .. 110) */
    fb_fill(6u, 46u, 228u, 64u, c_bar_bg);
    fb_rect_outline(6u, 46u, 228u, 64u, c_border);
    /* Center zero line */
    fb_fill(8u, 78u, 224u, 1u, rgb565_be(34, 40, 48));

    sw_live();
    uint32_t pv0 = sw_preview(0u);
    uint32_t pv1 = sw_preview(1u);
    if (pv1 > 0u && pv1 <= SHRUB_WASM_MEM_BYTES - 512u) {
        int32_t prev_y = 78;
        for (uint32_t x = 0; x < 224u; x++) {
            uint32_t idx = (x * 256u) / 224u;
            int16_t s = (int16_t)w_ld_s16(pv1 + idx * 2u);
            int32_t y = 78 - ((int32_t)s * 26) / 32768;
            if (y < 48) y = 48;
            if (y > 107) y = 107;
            int32_t y0 = y < prev_y ? y : prev_y;
            int32_t y1 = y > prev_y ? y : prev_y;
            fb_fill(8u + x, (uint32_t)y0, 1u, (uint32_t)(y1 - y0 + 1), c_green2);
            prev_y = y;
        }
    }
    if (pv0 > 0u && pv0 <= SHRUB_WASM_MEM_BYTES - 512u) {
        int32_t prev_y = 78;
        for (uint32_t x = 0; x < 224u; x++) {
            uint32_t idx = (x * 256u) / 224u;
            int16_t s = (int16_t)w_ld_s16(pv0 + idx * 2u);
            int32_t y = 78 - ((int32_t)s * 26) / 32768;
            if (y < 48) y = 48;
            if (y > 107) y = 107;
            int32_t y0 = y < prev_y ? y : prev_y;
            int32_t y1 = y > prev_y ? y : prev_y;
            fb_fill(8u + x, (uint32_t)y0, 1u, (uint32_t)(y1 - y0 + 1), c_green);
            prev_y = y;
        }
    }

    /* Page title + status indicators (voices, oct, play/rec) */
    const ShrubGroupDef *grp = &SHRUB_GROUPS[shrub_cur_group];
    uint32_t sub = shrub_cur_sub[shrub_cur_group] % grp->sub_count;
    const ShrubPageDef *pg = &grp->subs[sub];

    fb_draw_text(6u, 116u, pg->title, c_green, 1u, 14u);

    char st[16];
    uint32_t voices = sw_voices();
    int oct_disp = 3 + shrub_octave;
    st[0] = 'V';
    st[1] = (char)('0' + (voices % 10u));
    st[2] = ' ';
    st[3] = 'O';
    st[4] = (char)('0' + (oct_disp < 0 ? 0 : (oct_disp > 9 ? 9 : oct_disp)));
    st[5] = ' ';
    st[6] = shrub_playing ? '>' : '-';
    st[7] = shrub_recording ? 'R' : ' ';
    st[8] = 0;
    fb_draw_text(168u, 116u, st, shrub_recording ? c_amber : c_dim, 1u, 9u);

    /* 4 Parameter cards (2x2 grid: y=130..180, y=184..234) */
    for (uint32_t k = 0; k < 4u; k++) {
        uint32_t col = k & 1u;
        uint32_t row = k >> 1;
        uint32_t cx = 6u + col * 116u;
        uint32_t cy = 130u + row * 54u;
        uint32_t cw = 112u;
        uint32_t ch = 48u;
        uint32_t param = pg->params[k];

        fb_fill(cx, cy, cw, ch, c_card);
        fb_rect_outline(cx, cy, cw, ch, c_border);

        const char *lbl = wasm_cstr(sw_label(param));
        const char *val_txt = wasm_cstr(sw_text(param));
        uint32_t v = sw_value(param);
        if (v > 65535u) v = 65535u;

        fb_draw_text(cx + 5u, cy + 5u, lbl, c_dim, 1u, 12u);
        fb_draw_text(cx + 5u, cy + 19u, val_txt, c_white, 1u, 12u);

        fb_fill(cx + 5u, cy + 36u, cw - 10u, 6u, c_bar_bg);
        uint32_t fill_w = ((cw - 10u) * v) / 65535u;
        if (fill_w > 0u) {
            fb_fill(cx + 5u, cy + 36u, fill_w, 6u, c_green);
        }
    }

    web_draws++;
}

static void shrub_draw_splash_or_ui(void)
{
    if (shrub_splash_active && shrub_ms < 2900u) {
        uint32_t ptr = sw_splash_frame(shrub_ms);
        if (ptr > 0u && ptr <= SHRUB_WASM_MEM_BYTES - sizeof(web_fb)) {
            memcpy(web_fb, wasm_mem + ptr, sizeof(web_fb));
            web_draws++;
            return;
        }
    }
    shrub_splash_active = 0u;
    shrub_draw_dashboard();
}

/* ------------------------------------------------------------ NOR flash save/load --- */
static int32_t shrub_save_now(void)
{
    if (!shrub_booted) return -1;
    uint32_t cur_gen = sw_gen();
    uint32_t cur_seq = sw_seq_version();
    if (shrub_flash_writes > 0u &&
        cur_gen == shrub_saved_gen &&
        cur_seq == shrub_saved_seq_ver &&
        shrub_preset_idx == shrub_saved_preset) {
        return 0;
    }

    uint32_t buf_ptr = sw_buf();
    uint32_t buf_sz = sw_buf_size();
    uint32_t plen = sw_encode();
    if (plen > 16384u || plen > buf_sz || buf_ptr + plen > SHRUB_WASM_MEM_BYTES) return -1;
    memcpy(nor + 32u, wasm_mem + buf_ptr, plen);

    uint32_t slen = sw_seq_encode();
    if (slen > 16384u || slen > buf_sz || buf_ptr + slen > SHRUB_WASM_MEM_BYTES) slen = 0u;
    if (slen > 0u) memcpy(nor + 32u + plen, wasm_mem + buf_ptr, slen);

    nor_wr32(0u, SHRUB_NOR_MAGIC);
    nor_wr32(4u, SHRUB_NOR_VERSION);
    nor_wr32(8u, shrub_preset_idx);
    nor_wr32(12u, (uint32_t)shrub_octave);
    nor_wr32(16u, shrub_cur_group);
    nor_wr32(20u, shrub_cur_sub[shrub_cur_group]);
    nor_wr32(24u, plen);
    nor_wr32(28u, slen);

    shrub_saved_gen = cur_gen;
    shrub_saved_seq_ver = cur_seq;
    shrub_saved_preset = shrub_preset_idx;
    shrub_flash_writes++;
    return 1;
}

static void shrub_restore_from_nor(void)
{
    if (nor_rd32(0u) != SHRUB_NOR_MAGIC || nor_rd32(4u) != SHRUB_NOR_VERSION) return;
    uint32_t pidx = nor_rd32(8u);
    int32_t oct = (int32_t)nor_rd32(12u);
    uint32_t grp = nor_rd32(16u);
    uint32_t sub = nor_rd32(20u);
    uint32_t plen = nor_rd32(24u);
    uint32_t slen = nor_rd32(28u);

    uint32_t pcnt = sw_preset_count();
    if (pidx < pcnt) shrub_preset_idx = pidx;
    if (oct >= -3 && oct <= 3) shrub_octave = oct;
    if (grp < 9u) {
        shrub_cur_group = grp;
        shrub_cur_sub[grp] = sub % SHRUB_GROUPS[grp].sub_count;
    }

    uint32_t buf_ptr = sw_buf();
    uint32_t buf_sz = sw_buf_size();
    if (plen > 0u && plen <= 16384u && plen <= buf_sz && 32u + plen <= sizeof(nor)) {
        memcpy(wasm_mem + buf_ptr, nor + 32u, plen);
        sw_decode(plen);
    }
    if (slen > 0u && slen <= 16384u && slen <= buf_sz && 32u + plen + slen <= sizeof(nor)) {
        memcpy(wasm_mem + buf_ptr, nor + 32u + plen, slen);
        sw_seq_decode(slen);
    }
    sw_publish();
    sw_poll();
}

/* ------------------------------------------------------------ Fm1FirmwareApi HAL --- */
static uint8_t *web_nor(void) { return nor; }
static uint32_t web_nor_size(void) { return (uint32_t)sizeof(nor); }
static void web_nor_erase(void) { memset(nor, 0xFF, sizeof(nor)); }
static uint32_t web_flash_writes_count(void) { return shrub_flash_writes; }
static void web_seed(uint32_t s) { shrub_seed_val = s; }

static void web_boot(void)
{
    wasm_init_memory();
    shrub_wasm_initialize();
    sw_init();

    shrub_samples = 0u;
    shrub_ms = 0u;
    shrub_last_ui_ms = 0u;
    shrub_btn_mask = 0u;
    shrub_key_mask = 0u;
    shrub_octave = 0;
    memset(shrub_note_on, 0, sizeof(shrub_note_on));
    memset(shrub_note_midi, 0, sizeof(shrub_note_midi));
    shrub_cur_group = 0u;
    memset(shrub_cur_sub, 0, sizeof(shrub_cur_sub));
    shrub_preset_idx = 0u;
    shrub_playing = 0u;
    shrub_recording = 0u;

    sw_preset_load(0u);
    sw_publish();
    sw_poll();
    shrub_restore_from_nor();

    shrub_saved_gen = sw_gen();
    shrub_saved_seq_ver = sw_seq_version();
    shrub_saved_preset = shrub_preset_idx;
    shrub_last_dirty_ms = 0u;
    shrub_save_led_until_ms = 0u;

    uint32_t seed = shrub_seed_val ? shrub_seed_val : 42u;
    shrub_splash_active = sw_splash_begin(seed) ? 1u : 0u;
    shrub_booted = 1u;
    shrub_draw_splash_or_ui();
}

static void web_render(uint32_t n)
{
    if (n > 1024u) n = 1024u;
    if (!shrub_booted) {
        memset(out_l, 0, n * sizeof(float));
        memset(out_r, 0, n * sizeof(float));
        return;
    }

    float target = (float)shrub_master_adc / 1023.0f;
    target *= target;
    shrub_master_smooth += (target - shrub_master_smooth) * 0.2f;
    float gain = shrub_master_smooth;

    uint32_t done = 0u;
    while (done < n) {
        uint32_t chunk = n - done;
        if (chunk > 128u) chunk = 128u;
        sw_seq_poll(shrub_ms);
        uint32_t ptr = sw_render(chunk);
        sw_poll();
        if (ptr > 0u && ptr <= SHRUB_WASM_MEM_BYTES - chunk * 8u) {
            for (uint32_t i = 0; i < chunk; i++) {
                out_l[done + i] = w_ld_f32(ptr + i * 8u) * gain;
                out_r[done + i] = w_ld_f32(ptr + i * 8u + 4u) * gain;
            }
        } else {
            memset(out_l + done, 0, chunk * sizeof(float));
            memset(out_r + done, 0, chunk * sizeof(float));
        }
        shrub_samples += chunk;
        shrub_ms = (uint32_t)((shrub_samples * 1000ULL) / FS);
        done += chunk;
    }

    /* Update sequencer transport state from engine */
    uint32_t seq_st = sw_seq_state();
    shrub_playing = (seq_st & 1u) ? 1u : 0u;
    shrub_recording = (seq_st & 2u) ? 1u : 0u;

    /* Background autosave after 5 s idle if patch/lanes changed */
    if (sw_gen() != shrub_saved_gen || sw_seq_version() != shrub_saved_seq_ver || shrub_preset_idx != shrub_saved_preset) {
        if (shrub_last_dirty_ms == 0u) shrub_last_dirty_ms = shrub_ms;
        else if (shrub_ms - shrub_last_dirty_ms >= 5000u) {
            shrub_save_now();
            shrub_last_dirty_ms = 0u;
        }
    } else {
        shrub_last_dirty_ms = 0u;
    }

    if (shrub_ms - shrub_last_ui_ms >= 33u) {
        shrub_last_ui_ms = shrub_ms;
        shrub_draw_splash_or_ui();
    }
}

static float *web_out_l(void) { return out_l; }
static float *web_out_r(void) { return out_r; }

static void shrub_release_all_keys(void)
{
    for (uint32_t k = 0; k < 27u; k++) {
        if (shrub_note_on[k]) {
            uint8_t mn = (uint8_t)shrub_note_midi[k];
            sw_seq_key(mn, 0u);
            sw_note(mn, 0u);
            shrub_note_on[k] = 0u;
        }
    }
}

static void web_buttons(uint32_t mask)
{
    uint32_t old = shrub_btn_mask;
    uint32_t rising = mask & ~old;
    shrub_btn_mask = mask;
    if (!rising) return;

    shrub_splash_active = 0u;

    /* Check page buttons */
    for (uint32_t g = 0; g < 9u; g++) {
        if ((rising >> SHRUB_GROUPS[g].btn) & 1u) {
            if (shrub_cur_group == g) {
                shrub_cur_sub[g] = (shrub_cur_sub[g] + 1u) % SHRUB_GROUPS[g].sub_count;
            } else {
                shrub_cur_group = g;
            }
            shrub_draw_dashboard();
            return;
        }
    }

    if ((rising >> B_OCTDN) & 1u) {
        if (shrub_octave > -3) {
            shrub_release_all_keys();
            shrub_octave--;
            shrub_draw_dashboard();
        }
    }
    if ((rising >> B_OCTUP) & 1u) {
        if (shrub_octave < 3) {
            shrub_release_all_keys();
            shrub_octave++;
            shrub_draw_dashboard();
        }
    }
    if ((rising >> B_PLAY) & 1u) {
        uint32_t next_play = (sw_seq_state() & 1u) ? 0u : 1u;
        sw_seq_play(next_play);
        sw_transport(next_play);
        shrub_playing = next_play;
        if (!next_play) shrub_recording = 0u;
        shrub_draw_dashboard();
    }
    if ((rising >> B_REC) & 1u) {
        uint32_t st = sw_seq_state();
        uint32_t next_rec = (st & 2u) ? 0u : 1u;
        if (next_rec && !(st & 1u)) {
            sw_seq_play(1u);
            sw_transport(1u);
        }
        sw_seq_record(next_rec);
        shrub_recording = next_rec;
        shrub_draw_dashboard();
    }
    if ((rising >> B_SAVE) & 1u) {
        shrub_save_now();
        shrub_save_led_until_ms = shrub_ms + 300u;
        shrub_draw_dashboard();
    }
}

static void web_keys(uint32_t mask)
{
    mask &= (1u << 27) - 1u;
    uint32_t changed = mask ^ shrub_key_mask;
    shrub_key_mask = mask;
    if (!changed) return;

    shrub_splash_active = 0u;
    for (uint32_t k = 0; k < 27u; k++) {
        if (!((changed >> k) & 1u)) continue;
        uint32_t down = (mask >> k) & 1u;
        if (down) {
            int32_t mn = 53 + (int32_t)k + shrub_octave * 12;
            if (mn < 0) mn = 0;
            if (mn > 127) mn = 127;
            shrub_note_on[k] = 1u;
            shrub_note_midi[k] = (int8_t)mn;
            sw_seq_key((uint32_t)mn, 1u);
            sw_note((uint32_t)mn, 1u);
        } else if (shrub_note_on[k]) {
            uint8_t mn = (uint8_t)shrub_note_midi[k];
            shrub_note_on[k] = 0u;
            sw_seq_key(mn, 0u);
            sw_note(mn, 0u);
        }
    }
}

static void web_enc(uint32_t role, int32_t steps)
{
    if (!steps) return;
    shrub_splash_active = 0u;

    if (role == EN_PRESET) {
        uint32_t cnt = sw_preset_count();
        if (cnt > 0u) {
            int32_t idx = ((int32_t)shrub_preset_idx + steps) % (int32_t)cnt;
            if (idx < 0) idx += (int32_t)cnt;
            shrub_preset_idx = (uint32_t)idx;
            sw_preset_load(shrub_preset_idx);
            sw_publish();
            sw_poll();
        }
    } else if (role == EN_SELECT) {
        const ShrubGroupDef *grp = &SHRUB_GROUPS[shrub_cur_group];
        if (grp->sub_count > 1u) {
            int32_t s = ((int32_t)shrub_cur_sub[shrub_cur_group] + steps) % (int32_t)grp->sub_count;
            if (s < 0) s += (int32_t)grp->sub_count;
            shrub_cur_sub[shrub_cur_group] = (uint32_t)s;
        } else {
            int32_t g = ((int32_t)shrub_cur_group + steps) % 9;
            if (g < 0) g += 9;
            shrub_cur_group = (uint32_t)g;
        }
    } else if (role == EN_ALGO) {
        uint32_t tbl_param = (shrub_cur_group == 1u) ? 13u : 0u; /* OSC A or OSC B table */
        sw_adjust(tbl_param, (uint32_t)steps);
        sw_publish();
        sw_poll();
    } else if (role >= EN_K1 && role <= EN_K4) {
        uint32_t k = role - EN_K1;
        const ShrubGroupDef *grp = &SHRUB_GROUPS[shrub_cur_group];
        uint32_t sub = shrub_cur_sub[shrub_cur_group] % grp->sub_count;
        uint32_t param = grp->subs[sub].params[k];
        sw_adjust(param, (uint32_t)steps);
        sw_seq_touch(param, shrub_ms);
        sw_publish();
        sw_poll();
    }
    shrub_draw_dashboard();
}

static void web_master(uint32_t adc)
{
    shrub_master_adc = adc > 1023u ? 1023u : adc;
}

static uint32_t web_midi(uint32_t status, uint32_t d1, uint32_t d2)
{
    if (status < 0x80u || status >= 0xF0u) return 1u;
    sw_midi(status, d1 & 0x7Fu, d2 & 0x7Fu);
    sw_publish();
    sw_poll();
    return 1u;
}

static const uint16_t *web_screen(void) { return web_fb; }
static uint32_t web_screen_draws(void) { return web_draws; }

static uint32_t web_lit_buttons(void)
{
    uint32_t m = 1u << SHRUB_GROUPS[shrub_cur_group].btn;
    if (shrub_playing) m |= (1u << 14); /* green PLAY LED */
    if (shrub_recording) m |= (1u << B_REC);
    if (shrub_octave < 0) m |= (1u << B_OCTDN);
    if (shrub_octave > 0) m |= (1u << B_OCTUP);
    if (shrub_ms < shrub_save_led_until_ms) m |= (1u << B_SAVE);
    return m;
}

static uint32_t web_lit_keys(void)
{
    uint32_t m = shrub_key_mask;
    uint32_t g0 = sw_gated(0u);
    uint32_t g1 = sw_gated(1u);
    uint32_t g2 = sw_gated(2u);
    uint32_t g3 = sw_gated(3u);
    for (uint32_t k = 0; k < 27u; k++) {
        int32_t mn = 53 + (int32_t)k + shrub_octave * 12;
        if (mn < 0 || mn > 127) continue;
        uint32_t w = (uint32_t)mn >> 5;
        uint32_t b = (uint32_t)mn & 31u;
        uint32_t gw = (w == 0u) ? g0 : (w == 1u) ? g1 : (w == 2u) ? g2 : g3;
        if ((gw >> b) & 1u) m |= (1u << k);
    }
    return m;
}

static uint32_t web_dim_buttons(void)
{
    return ((1u << NB) - 1u) & ~web_lit_buttons();
}

static uint32_t web_dim_keys(void)
{
    return ((1u << 27) - 1u) & ~web_lit_keys();
}

static uint32_t web_breath_buttons(void) { return 0u; }
static uint32_t web_breath_keys(void) { return 0u; }
static uint32_t web_mid_keys(void) { return 0u; }
static uint32_t web_dim_level(void) { return 0u; }
static const uint8_t *web_anim_levels(void) { return 0; }
static uint32_t web_playing(void) { return shrub_playing; }
static uint32_t web_now_ms(void) { return shrub_ms; }
