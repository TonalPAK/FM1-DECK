/* SPDX-License-Identifier: GPL-3.0-only */
/* FM1 DECK HAL adapter for K-WASUB (upstream/kwasub/source/firmware/src/ksub.c with KAWA_WASM). */
#include <stdint.h>
#include <string.h>

#define KAWA_WASM 1
#ifndef APP_VER
#define APP_VER FELUCCA_VERSION
#endif

#define memset ksub_libc_memset
#define memcpy ksub_libc_memcpy
#define memcmp ksub_libc_memcmp
#include "ksub.c"
#undef memset
#undef memcpy
#undef memcmp

#undef NB
#undef NE
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

/* FM1_B_* (0..13) -> kwasub BTN_* (0:OCT-, 1:OCT+, 2:FX, 3:SEL, 4:ENV, 5:LFO, 6:EDIT, 7:GLO, 8:HOME, 9:SAVE, 10:ARP, 11:SEQ, 12:PLAY, 13:REC) */
static const uint8_t FM1_TO_KSUB_BTN[14] = { 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 0, 1 };
static const uint8_t FM1_TO_KSUB_ENC[7]  = { 0, 1, 6, 2, 3, 4, 5 };

static uint8_t nor[0x100000];
static uint16_t web_fb[240 * 240];
static float out_l[1024], out_r[1024];
static float ring_l[512], ring_r[512];
static uint32_t ring_w, ring_r_;
static uint32_t ksub_btn_mask, ksub_draws, ksub_seen_gen;
static uint32_t ksub_booted;

static uint32_t ksub_last_saved_gen = 0xFFFFFFFFu;

static int32_t ksub_pack_nor(void)
{
    uint32_t g = ksub_settings_gen();
    if (g == ksub_last_saved_gen) return 0;
    memcpy(nor, ksub_settings(), 260u);
    ksub_last_saved_gen = g;
    return 1;
}

static uint8_t *web_nor(void) { return nor; }
static uint32_t web_flash_writes_count(void) { return ksub_settings_gen(); }
static void web_nor_erase(void) { memset(nor, 0xFF, sizeof(nor)); }

static void web_boot(void)
{
    if (ksub_booted) return;
    memcpy(ksub_settings(), nor, 260u);
    ksub_init();
    ksub_step();
    ksub_last_saved_gen = ksub_settings_gen();
    ring_w = ring_r_ = 0;
    ksub_booted = 1;
}

static void web_render(uint32_t n)
{
    if (n > 1024u) n = 1024u;
    if (!ksub_booted) {
        memset(out_l, 0, n * sizeof(float));
        memset(out_r, 0, n * sizeof(float));
        return;
    }
    while (ring_w - ring_r_ < n) {
        const float *pcm = ksub_audio();
        for (uint32_t i = 0; i < HALF_FRAMES; i++) {
            uint32_t idx = (ring_w++) & 511u;
            ring_l[idx] = pcm[2u * i];
            ring_r[idx] = pcm[2u * i + 1u];
        }
        ksub_step();
    }
    for (uint32_t i = 0; i < n; i++) {
        uint32_t idx = (ring_r_++) & 511u;
        out_l[i] = ring_l[idx];
        out_r[i] = ring_r[idx];
    }
}

static float *web_out_l(void) { return out_l; }
static float *web_out_r(void) { return out_r; }

static void web_buttons(uint32_t mask)
{
    mask &= (1u << NB) - 1u;
    uint32_t diff = mask ^ ksub_btn_mask;
    for (int i = 0; i < NB; i++)
        if ((diff >> i) & 1u)
            ksub_button(FM1_TO_KSUB_BTN[i], (mask >> i) & 1u);
    ksub_btn_mask = mask;
}
static void web_keys(uint32_t mask) { ksub_keys(mask & 0x7FFFFFFu); }
static void web_enc(uint32_t role, int32_t steps)
{
    if (role < 7u) ksub_turn(FM1_TO_KSUB_ENC[role], steps);
}
static void web_master(uint32_t adc)
{
    if (adc > 1023u) adc = 1023u;
    ksub_master((int32_t)((adc * adc) >> 8));
}
static uint32_t web_midi(uint32_t status, uint32_t d1, uint32_t d2)
{
    ksub_midi(status, d1, d2);
    return 1u;
}
static uint32_t web_screen_draws(void)
{
    uint32_t g = ksub_screen_gen();
    if (g != ksub_seen_gen) {
        ksub_seen_gen = g;
        const uint16_t *src = ksub_screen();
        uint32_t changed = 0;
        for (uint32_t i = 0; i < 240u * 240u; i++) {
            uint16_t sw = (uint16_t)((src[i] >> 8) | (src[i] << 8));
            changed |= (uint32_t)(web_fb[i] ^ sw);
            web_fb[i] = sw;
        }
        if (changed) ksub_draws++;
    }
    return ksub_draws;
}
static const uint16_t *web_screen(void) { return web_fb; }
static uint32_t web_lit_buttons(void)
{
    const uint8_t *l = ksub_leds();
    uint32_t m = 0;
    for (int i = 0; i < NB; i++)
        if (l[FM1_TO_KSUB_BTN[i]]) m |= 1u << i;
    return m;
}
static uint32_t web_lit_keys(void)
{
    const uint8_t *l = ksub_leds();
    uint32_t m = 0;
    for (int i = 0; i < 27; i++)
        if (l[14 + i]) m |= 1u << i;
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
static uint32_t web_now_ms(void) { return fm1_ms; }
