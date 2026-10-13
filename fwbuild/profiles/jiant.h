/* SPDX-License-Identifier: GPL-3.0-only
 * FM1 DECK profile: JIANT FM (https://github.com/juanjiant-bit/JIANT), a Felucca fork with SLOOP's song system.
 * HAL = upstream web/emu/felucca_web.c (identical design; adds web_seed, LAYER_SAVE, LAYER_MACRO). */
#define FM1DECK_FW_ID "jiant"
#define FM1DECK_FW_NAME "JIANT FM"
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
#define FM1DECK_GLUE_SEED 1                     /* web_seed: this session's macros (worklet.js passes a random seed) */
#endif
#ifndef FM1DECK_GLUE_TEST_HEAVY
#define FM1DECK_GLUE_TEST_HEAVY 1
#endif
#define FD_HAS_LAYER_SAVE_MACRO 1
#if FM1DECK_GLUE_UI_STATE
#include "felucca_family_ui.h"
#endif
