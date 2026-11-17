/* SPDX-License-Identifier: GPL-3.0-only */
/* FM1 DECK HAL adapter for Arctic Scream (upstream/arctic/web/fm1_web.c). */
#include <stdint.h>
#include <string.h>

#define ARCTIC_WEB 1
#define APP_NAME "ARCTIC"
#ifndef APP_VER
#define APP_VER FELUCCA_VERSION
#endif

static void fm1_deck_noop_yield(void) {}
#define web_yield fm1_deck_noop_yield_impl
#define web_fb arc_emu_fb
/* Provide non-yielding platform_poll behavior by overriding web_yield/web_ticks after inclusion?
 * Wait: fm1_web.c declares extern web_yield/web_ticks; we implement them right below! */
#include "../../firmware/src/arctic.c"
#undef web_fb

void web_yield(void) {}
uint32_t web_ticks(void) { return fm1_ms * 24000u; }

#undef NB
#undef NE
#undef B_FX
#undef B_PLAY
#undef B_OCTUP
#undef EN_SELECT
#undef EN_K4
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
static const uint8_t FM1_TO_ARC_BTN[14] = { 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 0, 1 };
static const uint8_t FM1_TO_ARC_ENC[7]  = { 0, 1, 6, 2, 3, 4, 5 };

static uint8_t nor[0x100000];
static uint16_t web_fb[240 * 240];
static uint32_t arc_draws, arc_seen_draws, arc_booted, arc_last_saved_writes = 0xFFFFFFFFu;
#include "../common/store_nor.h"
#define ARC_MAGIC 0x53435241u

static int32_t arc_pack_nor(void)
{
    uint32_t w = web_settings_writes();
    if (w == arc_last_saved_writes) return 0;
    int32_t len = web_settings_len();
    fd_nor_wr32(nor, ARC_MAGIC);
    fd_nor_wr32(nor + 4u, (uint32_t)len);
    if (len > 0 && len <= 256)
        memcpy(nor + 8u, web_settings(), (size_t)len);
    if (8u + 256u < sizeof(nor))
        memset(nor + 8u + (len > 0 && len <= 256 ? (size_t)len : 0u), 0xFF, sizeof(nor) - 8u - (len > 0 && len <= 256 ? (size_t)len : 0u));
    arc_last_saved_writes = w;
    return 1;
}

static uint8_t *web_nor(void) { return nor; }
static uint32_t web_flash_writes_count(void) { return web_settings_writes(); }
static void web_nor_erase(void) { memset(nor, 0xFF, sizeof(nor)); }

static void web_boot(void)
{
    if (arc_booted) return;
    memset(fake_flash, 0xFF, sizeof(fake_flash));
    if (fd_nor_rd32(nor) == ARC_MAGIC) {
        int32_t len = (int32_t)fd_nor_rd32(nor + 4u);
        if (len >= 0 && len <= 256) {
            if (len > 0) memcpy(web_settings(), nor + 8u, (size_t)len);
            web_settings_set_len(len);
        }
    }
    app_start();
    arc_last_saved_writes = web_settings_writes();
    arc_booted = 1;
}

static void web_render(uint32_t n)
{
    if (n > 1024u) n = 1024u;
    if (!arc_booted) {
        memset(out_l, 0, n * sizeof(float));
        memset(out_r, 0, n * sizeof(float));
        return;
    }
    web_want_frames(n);
    while (!web_ready())
        app_step();
    web_take();
}

static void arc_buttons(uint32_t mask)
{
    uint32_t mapped = 0;
    for (int i = 0; i < 14; i++)
        if ((mask >> i) & 1u) mapped |= 1u << FM1_TO_ARC_BTN[i];
    web_buttons(mapped);
}
static void arc_enc(uint32_t role, int32_t steps)
{
    if (role < 7u) web_enc(FM1_TO_ARC_ENC[role], steps);
}
#define web_buttons arc_buttons
#define web_enc arc_enc

static uint32_t arc_screen_draws(void)
{
    uint32_t d = web_draws;
    if (d != arc_seen_draws) {
        arc_seen_draws = d;
        uint32_t changed = 0;
        for (uint32_t i = 0; i < 240u * 240u; i++) {
            uint16_t sw = (uint16_t)((fb[i] >> 8) | (fb[i] << 8));
            changed |= (uint32_t)(web_fb[i] ^ sw);
            web_fb[i] = sw;
        }
        if (changed) arc_draws++;
    }
    return arc_draws;
}
#define web_screen_draws arc_screen_draws
static const uint16_t *web_screen(void) { return web_fb; }
static uint32_t arc_lit_buttons(void)
{
    uint32_t raw = web_led_btn, m = 0;
    for (int i = 0; i < 14; i++)
        if ((raw >> FM1_TO_ARC_BTN[i]) & 1u) m |= 1u << i;
    return m;
}
#define web_lit_buttons arc_lit_buttons
static uint32_t web_dim_buttons(void) { return 0u; }
static uint32_t web_dim_keys(void) { return 0u; }
static uint32_t web_breath_buttons(void) { return 0u; }
static uint32_t web_breath_keys(void) { return 0u; }
static uint32_t web_mid_keys(void) { return 0u; }
static uint32_t web_dim_level(void) { return 0u; }
static const uint8_t *web_anim_levels(void) { return 0; }
static uint32_t web_playing(void) { return 0u; }
static uint32_t arc_midi(uint32_t status, uint32_t d1, uint32_t d2)
{
    web_midi(status, d1, d2);
    return 1u;
}
#define web_midi arc_midi
