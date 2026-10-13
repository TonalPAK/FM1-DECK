/* SPDX-License-Identifier: GPL-3.0-only
 * FM1 DECK profile: FELUCCA (https://github.com/hugelton/Felucca), HAL = upstream web/emu/felucca_web.c.
 * Every FM1DECK_GLUE_* can be overridden with -D...=0 (CI does so when a capability stops compiling). */
#define FM1DECK_FW_ID "felucca"
#define FM1DECK_FW_NAME "FELUCCA"
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
#define FM1DECK_GLUE_SEED 0                     /* web_seed is JIANT's (0.4+) */
#endif
#ifndef FM1DECK_GLUE_TEST_HEAVY
#define FM1DECK_GLUE_TEST_HEAVY 1
#endif
#define FD_HAS_LAYER_SAVE_MACRO 0               /* Felucca: LAYER_NONE..LAYER_REC */
#if FM1DECK_GLUE_UI_STATE
#include "felucca_family_ui.h"
#endif
