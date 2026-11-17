/* SPDX-License-Identifier: GPL-3.0-only */
#define FM1DECK_FW_ID "arctic"
#define FM1DECK_FW_NAME "ARCTIC SCREAM"
#define FM1DECK_GLUE_HOST_CLOCK 0
#define FM1DECK_GLUE_SAVE_NOW 1
#define FM1DECK_GLUE_UI_STATE 0
#define FM1DECK_GLUE_SEED 0
#define FM1DECK_GLUE_TEST_HEAVY 0
#define FM1DECK_CUSTOM_SAVE_NOW 1
static int32_t fd_save_now(void) { return arc_pack_nor(); }
