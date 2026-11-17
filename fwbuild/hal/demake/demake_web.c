/* SPDX-License-Identifier: GPL-3.0-only */
/* FM1 DECK HAL adapter for Demake (upstream/demake/web/dmk_web.c). */
#include <stdint.h>
#include <string.h>

#include "../../web/dmk_web.c"

#define FS 44100u
#define NB 14
#define NE 7
#define B_FX 0
#define B_PLAY 10
#define B_OCTUP 13
#define EN_SELECT 0
#define EN_K4 6

static const char *const B_NAME[NB] = {
    "FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC", "OCT-", "OCT+"
};

static uint8_t nor[0x100000];
static uint16_t web_fb[240 * 240];
static float out_l[1024], out_r[1024];
static float ring_l[256], ring_r[256];
static uint32_t ring_w, ring_r_;
static uint32_t dmk_btn_mask, dmk_key_mask;
static uint32_t dmk_master_q12 = 2048u;
static uint32_t dmk_ms, dmk_last_frame_ms = 0xFFFFFFFFu, dmk_draws;
static uint64_t dmk_frames;

static uint32_t dmk_last_saved_gen = 0xFFFFFFFFu;

static int32_t dmk_pack_nor(void)
{
    uint32_t g = w_store_gen();
    if (g == dmk_last_saved_gen) return 0;
    memcpy(nor, w_store(), 65536u);
    dmk_last_saved_gen = g;
    return 1;
}

static uint8_t *web_nor(void) { return nor; }
static uint32_t web_flash_writes_count(void) { return w_store_gen(); }
static void web_nor_erase(void) { memset(nor, 0xFF, sizeof(nor)); }

static void web_boot(void)
{
    memcpy(w_store(), nor, 65536u);
    w_init(0xC0FFEE42u);
    dmk_last_saved_gen = w_store_gen();
    dmk_frames = 0;
    dmk_ms = 0;
    dmk_last_frame_ms = 0xFFFFFFFFu;
    ring_w = ring_r_ = 0;
}

static void web_render(uint32_t n)
{
    if (n > 1024u) n = 1024u;
    float gain = (float)dmk_master_q12 / 4096.0f;
    while (ring_w - ring_r_ < n) {
        w_render();
        const int16_t *pcm = w_audio();
        for (uint32_t i = 0; i < 128u; i++) {
            uint32_t idx = (ring_w++) & 255u;
            ring_l[idx] = ((float)pcm[2u * i] / 32768.0f) * gain;
            ring_r[idx] = ((float)pcm[2u * i + 1u] / 32768.0f) * gain;
        }
        dmk_frames += 128u;
        dmk_ms = (uint32_t)((dmk_frames * 1000u) / FS);
    }
    for (uint32_t i = 0; i < n; i++) {
        uint32_t idx = (ring_r_++) & 255u;
        out_l[i] = ring_l[idx];
        out_r[i] = ring_r[idx];
    }
}

static float *web_out_l(void) { return out_l; }
static float *web_out_r(void) { return out_r; }

static void web_buttons(uint32_t mask)
{
    mask &= (1u << NB) - 1u;
    uint32_t diff = mask ^ dmk_btn_mask;
    for (int i = 0; i < NB; i++)
        if ((diff >> i) & 1u)
            w_button(i, (int)((mask >> i) & 1u));
    dmk_btn_mask = mask;
}

static void web_keys(uint32_t mask)
{
    mask &= (1u << 27) - 1u;
    uint32_t diff = mask ^ dmk_key_mask;
    for (int i = 0; i < 27; i++)
        if ((diff >> i) & 1u)
            w_key(i, (int)((mask >> i) & 1u));
    dmk_key_mask = mask;
}

static void web_enc(uint32_t role, int32_t steps) { w_encoder((int)role, steps); }
static void web_master(uint32_t adc)
{
    if (adc > 1023u) adc = 1023u;
    dmk_master_q12 = (adc * adc) >> 8;
}
static uint32_t web_midi(uint32_t status, uint32_t d1, uint32_t d2)
{
    w_midi((int)status, (int)d1, (int)d2);
    return 1u;
}

static uint32_t web_screen_draws(void)
{
    if (dmk_ms != dmk_last_frame_ms && (dmk_last_frame_ms == 0xFFFFFFFFu || dmk_ms - dmk_last_frame_ms >= 15u)) {
        dmk_last_frame_ms = dmk_ms;
        const uint8_t *src = w_frame(dmk_ms);
        const uint32_t *pal = w_palette();
        uint16_t p565[16];
        for (int p = 0; p < 16; p++) {
            uint32_t rgb = pal[p];
            uint32_t r = (rgb >> 16) & 0xFFu;
            uint32_t g = (rgb >> 8) & 0xFFu;
            uint32_t b = rgb & 0xFFu;
            uint16_t c = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            p565[p] = (uint16_t)((c >> 8) | (c << 8));
        }
        uint32_t changed = 0;
        for (uint32_t i = 0; i < 240u * 240u / 2u; i++) {
            uint8_t byte = src[i];
            uint16_t c0 = p565[byte >> 4];
            uint16_t c1 = p565[byte & 0x0Fu];
            changed |= (uint32_t)(web_fb[2u * i] ^ c0) | (uint32_t)(web_fb[2u * i + 1u] ^ c1);
            web_fb[2u * i] = c0;
            web_fb[2u * i + 1u] = c1;
        }
        if (changed) dmk_draws++;
    }
    return dmk_draws;
}
static const uint16_t *web_screen(void) { return web_fb; }
static uint32_t web_lit_buttons(void)
{
    const uint8_t *l = w_leds();
    uint32_t m = 0;
    for (int i = 0; i < NB; i++)
        if (l[27 + i]) m |= 1u << i;
    return m;
}
static uint32_t web_lit_keys(void)
{
    const uint8_t *l = w_leds();
    uint32_t m = 0;
    for (int i = 0; i < 27; i++)
        if (l[i]) m |= 1u << i;
    return m;
}
static uint32_t web_dim_buttons(void) { return 0u; }
static uint32_t web_dim_keys(void) { return 0u; }
static uint32_t web_breath_buttons(void) { return 0u; }
static uint32_t web_breath_keys(void) { return 0u; }
static uint32_t web_mid_keys(void) { return 0u; }
static uint32_t web_dim_level(void) { return 0u; }
static const uint8_t *web_anim_levels(void) { return 0; }
static uint32_t web_playing(void) { return 0u; }
static uint32_t web_now_ms(void) { return dmk_ms; }
