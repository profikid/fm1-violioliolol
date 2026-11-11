/* SPDX-License-Identifier: MIT
 * fm1-violioliolol: the app's half of the contract with fm1-base
 * (device/unity.c, host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "VIOLIOLIOLOL"
#define APP_TAGLINE "PHYSICAL STRINGS"
#define APP_BLOCK 256u                 /* frames per audio block: 5.8 ms at 44.1 kHz (the pinned fm1-base contract) */
#define APP_CPU_KHZ                    /* expose the platform CPU clock diagnostic */
#define APP_TOUCH                      /* velocity and pressure from the keys (lib/touch.h) */
#define APP_OVERLOAD                   /* the render over 90 % of its block: shed a voice */
#define APP_CANVAS_PX (240u * 72u)     /* the screen is drawn in bands of at most 72 rows: 34 KB of the pool */

/* The store. An object's number is written in its record: these never change.
 * fm1-base owns the panel calibration object. Two 4 KiB sectors (A/B) each. */
enum { OBJ_PANEL, OBJ_SETTINGS, OBJ_BANK, OBJ_NOBJ };
#define APP_STORE_MAP {0xDC000u, 0xFC000u, 0xDE000u}   /* the template's offsets */

/* fm1-base application contract. */
void app_init(void);                   /* boot: the engine, then the saved settings and presets */
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);   /* audio interrupt: n stereo frames, 24-bit */
void app_panic(void);                  /* silence now (before an update rewrites the flash) */
void app_ui_init(void);
void app_frame(void);                  /* the UI: input and a redraw */
void app_input_only(void);
void app_say(const char *a, const char *b);
void app_overload(void);               /* audio interrupt, after a render that took too long */

/* the host simulator's checks (host/plat_host.c "expect"): see app.c app_expect */
#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
