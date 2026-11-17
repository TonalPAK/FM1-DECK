/* SPDX-License-Identifier: GPL-3.0-only */
#define FM1DECK_FW_ID "guru600"
#define FM1DECK_FW_NAME "GURU-600"
#define FM1DECK_CLOCK_PARAM G_CLOCK
#ifndef FM1DECK_GLUE_HOST_CLOCK
#define FM1DECK_GLUE_HOST_CLOCK 1
#endif
#ifndef FM1DECK_GLUE_SAVE_NOW
#define FM1DECK_GLUE_SAVE_NOW 1
#endif
#ifndef FM1DECK_GLUE_UI_STATE
#define FM1DECK_GLUE_UI_STATE 1
#endif
#ifndef FM1DECK_GLUE_SEED
#define FM1DECK_GLUE_SEED 0
#endif
#ifndef FM1DECK_GLUE_TEST_HEAVY
#define FM1DECK_GLUE_TEST_HEAVY 0
#endif
#define FD_HAS_LAYER_SAVE_MACRO 0
#if FM1DECK_GLUE_UI_STATE
#include "felucca_family_ui.h"
#endif
