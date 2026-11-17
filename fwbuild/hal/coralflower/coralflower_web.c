/* SPDX-License-Identifier: GPL-3.0-only */
/* FM1 DECK HAL adapter for CoralFlower (upstream/coralflower/tools/emu/web/emu_web.c). */
#include <stdint.h>
#include <string.h>

#define web_boot   cf_emu_boot
#define web_fb     cf_emu_fb
#define web_midi   cf_emu_midi
#include "../../tools/emu/web/emu_web.c"
#undef web_boot
#undef web_fb
#undef web_midi

#define nor emu_flash
static uint16_t web_fb[240 * 240];

static uint8_t *web_nor(void) { return emu_flash; }
static uint32_t web_flash_writes_count(void) { return web_flash_dirty(); }
static void web_nor_erase(void) { memset(emu_flash, 0xFF, sizeof(emu_flash)); }
static void web_boot(void)
{
    memcpy(web_flash_stage(), emu_flash, sizeof(emu_flash));
    web_flash_stage_len(sizeof(emu_flash));
    cf_emu_boot();
}
static uint32_t web_midi(uint32_t status, uint32_t d1, uint32_t d2)
{
    uint32_t pkt;
    if (status >= 0xF8u)
        pkt = 0x0Fu | (status << 8);
    else if (status >= 0x80u && status < 0xF0u)
        pkt = ((status >> 4) & 0xFu) | (status << 8) | ((d1 & 0x7Fu) << 16) | ((d2 & 0x7Fu) << 24);
    else
        return 1u;
    return (uint32_t)web_midi_in(pkt);
}
static const uint16_t *web_screen(void)
{
    memcpy(web_fb, emu_hal.lcd, sizeof(web_fb));
    return web_fb;
}
static uint32_t web_screen_draws(void) { return web_lcd_writes(); }
static uint32_t web_lit_buttons(void)
{
    const uint8_t *l = web_leds();
    uint32_t m = 0;
    for (uint32_t i = 0; i < (uint32_t)NB; i++)
        if (l[EMU_NKEY + i] == 2u)
            m |= 1u << i;
    if (l[41] == 2u)
        m |= 1u << 14;
    return m;
}
static uint32_t web_lit_keys(void)
{
    const uint8_t *l = web_leds();
    uint32_t m = 0;
    for (uint32_t i = 0; i < 27u; i++)
        if (l[i] == 2u)
            m |= 1u << i;
    return m;
}
static uint32_t web_dim_buttons(void)
{
    const uint8_t *l = web_leds();
    uint32_t m = 0;
    for (uint32_t i = 0; i < (uint32_t)NB; i++)
        if (l[EMU_NKEY + i] == 1u)
            m |= 1u << i;
    return m;
}
static uint32_t web_dim_keys(void)
{
    const uint8_t *l = web_leds();
    uint32_t m = 0;
    for (uint32_t i = 0; i < 27u; i++)
        if (l[i] == 1u || l[i] == 4u)
            m |= 1u << i;
    return m;
}
static uint32_t web_mid_keys(void)
{
    const uint8_t *l = web_leds();
    uint32_t m = 0;
    for (uint32_t i = 0; i < 27u; i++)
        if (l[i] == 3u)
            m |= 1u << i;
    return m;
}
static uint32_t web_breath_buttons(void) { return 0u; }
static uint32_t web_breath_keys(void) { return 0u; }
static uint32_t web_dim_level(void) { return 1600u; }
static const uint8_t *web_anim_levels(void) { return 0; }
static uint32_t web_playing(void) { return (uint32_t)song.playing; }
static uint32_t web_now_ms(void) { return web_ms(); }
