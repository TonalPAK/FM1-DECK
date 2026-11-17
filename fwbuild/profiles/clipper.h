/* SPDX-License-Identifier: GPL-3.0-only */
#define FM1DECK_FW_ID "clipper"
#define FM1DECK_FW_NAME "CLIPPER"
#define FM1DECK_CLOCK_PARAM G_SYNC
#ifndef FM1DECK_GLUE_HOST_CLOCK
#define FM1DECK_GLUE_HOST_CLOCK 1
#endif
#ifndef FM1DECK_GLUE_SAVE_NOW
#define FM1DECK_GLUE_SAVE_NOW 1
#endif
#ifndef FM1DECK_GLUE_UI_STATE
#define FM1DECK_GLUE_UI_STATE 0
#endif
#ifndef FM1DECK_GLUE_SEED
#define FM1DECK_GLUE_SEED 0
#endif
#ifndef FM1DECK_GLUE_TEST_HEAVY
#define FM1DECK_GLUE_TEST_HEAVY 0
#endif
#define FM1DECK_CUSTOM_SAVE_NOW 1
#if FM1DECK_GLUE_SAVE_NOW
static int32_t fd_save_now(void)
{
#if FELUCCA_FLASH
    if (!flash_ok) return -1;
    sections_write();
    proj_capture(&autosave_buf);
    if (autosave_buf.sum == autosave_hash) return 0;
    if (st_save(OBJ_AUTOSAVE, &autosave_buf, sizeof autosave_buf) != 0) return -1;
    autosave_hash = autosave_buf.sum;
    autosave_ms = fm1_ms;
    return 1;
#else
    return -1;
#endif
}
#endif
