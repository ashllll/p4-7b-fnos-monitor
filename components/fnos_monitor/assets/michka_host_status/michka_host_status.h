/* SPDX-License-Identifier: CC-BY-NC-4.0
 * Michka-derived host-status images; see LICENSE and README.md.
 */
#pragma once

#include "lvgl.h"

#define MICHKA_HOST_STATUS_WIDTH  18U
#define MICHKA_HOST_STATUS_HEIGHT 18U

#ifdef __cplusplus
extern "C" {
#endif

extern const lv_image_dsc_t michka_host_status_online;
extern const lv_image_dsc_t michka_host_status_offline;

#ifdef __cplusplus
}
#endif
