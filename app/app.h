/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "fm1-violioliolol"
#define APP_BLOCK 256u

/* fm1-base owns the panel calibration object. */
enum { OBJ_PANEL, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u}

/* fm1-base application contract. */
void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);
