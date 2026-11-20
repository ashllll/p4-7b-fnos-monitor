/* IDF 5.5 compatibility for LVGL 9.6's PPA draw buffer cache helper.
 * LVGL 9.6 calls esp_cache_get_line_size_by_addr(), added in IDF 6.0.
 * Keep this declaration/replacement local to the LVGL target (see main/CMakeLists.txt);
 * it mirrors esp_lvgl_adapter's version branch and is inert on IDF >= 6.0.
 */
#ifndef FNOS_PPA_IDF55_COMPAT_H
#define FNOS_PPA_IDF55_COMPAT_H

#include "sdkconfig.h"
#include "esp_idf_version.h"

#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(6, 0, 0) && defined(CONFIG_SOC_PPA_SUPPORTED) && defined(CONFIG_LV_USE_PPA)
#include <stddef.h>
#include "esp_cache.h"
#include "esp_private/esp_cache_private.h"
#include "esp_memory_utils.h"
#include "esp_heap_caps.h"

static inline size_t esp_cache_get_line_size_by_addr(const void *addr)
{
    if (addr == NULL) {
        return 0;
    }
    uint32_t caps = esp_ptr_external_ram(addr) ? MALLOC_CAP_SPIRAM :
                    esp_ptr_internal(addr)      ? MALLOC_CAP_INTERNAL : 0;
    size_t align = 0;
    if (caps == 0 || esp_cache_get_alignment(caps, &align) != ESP_OK) {
        return 0;
    }
    return align;
}
#endif

#endif
