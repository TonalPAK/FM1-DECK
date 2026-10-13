/* SPDX-License-Identifier: GPL-3.0-only
 * FM1 DECK profile: SLOOP (https://github.com/isod89/sloop-fm1, by isod89, based on Felucca). SLOOP ships no browser
 * emulator; the HAL is fwbuild/hal/sloop/sloop_web.c (adapted 2026-10-11 from Felucca's felucca_web.c).
 * SLOOP's SYNC global picks the clock (its G_CLOCK is the metronome). Its own autosave path (persist_flush_now's
 * project half) replaces the Felucca one. UI_STATE is off until P4 maps SLOOP's ui (LY_* layers, its own PAGES). */
#define FM1DECK_FW_ID "sloop"
#define FM1DECK_FW_NAME "SLOOP"
#define FM1DECK_CLOCK_PARAM G_SYNC
#ifndef FM1DECK_GLUE_HOST_CLOCK
#define FM1DECK_GLUE_HOST_CLOCK 1
#endif
#ifndef FM1DECK_GLUE_SAVE_NOW
#define FM1DECK_GLUE_SAVE_NOW 1
#endif
#ifndef FM1DECK_GLUE_UI_STATE
#define FM1DECK_GLUE_UI_STATE 0                 /* TODO(P4): LY_PLAY..LY_SONG -> FM1_LAYER_*, SLOOP PAGES/page_desc */
#endif
#ifndef FM1DECK_GLUE_SEED
#define FM1DECK_GLUE_SEED 0
#endif
#ifndef FM1DECK_GLUE_TEST_HEAVY
#define FM1DECK_GLUE_TEST_HEAVY 0               /* sloop_web.c has no web_test_heavy: P5 adds a bench song of ours */
#endif
#define FM1DECK_CUSTOM_SAVE_NOW 1

#if FM1DECK_GLUE_SAVE_NOW
/* project.c persist_flush_now's project half: the working project into OBJ_AUTOSAVE (autosave_resume loads it at
 * power-on), and the live sections and the song (RAM-only until a quiet moment). From the earlier GGBOX port. */
static int32_t fd_save_now(void)
{
#if FELUCCA_FLASH
    if (!flash_ok)
        return -1;
    sections_write();
    proj_capture(&autosave_buf);
    if (autosave_buf.sum == autosave_hash)
        return 0;
    if (st_save(OBJ_AUTOSAVE, &autosave_buf, sizeof autosave_buf) != 0)
        return -1;
    autosave_hash = autosave_buf.sum;
    autosave_ms = fm1_ms;
    return 1;
#else
    return -1;
#endif
}
#endif
