/* Link-time probes: no managed dependency changes, no altered return values.
 * These calls are synchronous in the current P4/TRIPLE_PARTIAL adapter path.
 * Private pipeline pointers remain opaque; no private layout is copied here. */
#include "fnos_perf.h"
#include "esp_timer.h"
#include "esp_lcd_panel_ops.h"
#include "driver/ppa.h"
#include "common/display_bridge_common.h"
#include "src/draw/sw/lv_draw_sw.h"
#include "src/draw/sw/blend/lv_draw_sw_blend.h"

esp_err_t __real_ppa_do_scale_rotate_mirror(ppa_client_handle_t client,
                                           const ppa_srm_oper_config_t *config);
esp_err_t __wrap_ppa_do_scale_rotate_mirror(ppa_client_handle_t client,
                                           const ppa_srm_oper_config_t *config)
{
    int64_t start = esp_timer_get_time();
    esp_err_t ret = __real_ppa_do_scale_rotate_mirror(client, config);
    fnos_perf_note_stage(FNOS_PERF_PPA, (uint32_t)(esp_timer_get_time() - start));
    return ret;
}

esp_err_t __real_display_bridge_dma2d_copy_sync(void *desc, uint32_t timeout_ms);
esp_err_t __wrap_display_bridge_dma2d_copy_sync(void *desc, uint32_t timeout_ms)
{
    int64_t start = esp_timer_get_time();
    esp_err_t ret = __real_display_bridge_dma2d_copy_sync(desc, timeout_ms);
    fnos_perf_note_stage(FNOS_PERF_DMA2D, (uint32_t)(esp_timer_get_time() - start));
    return ret;
}

esp_err_t __real_esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t panel,
    int x_start, int y_start, int x_end, int y_end, const void *data);
esp_err_t __wrap_esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t panel,
    int x_start, int y_start, int x_end, int y_end, const void *data)
{
    int64_t start = esp_timer_get_time();
    esp_err_t ret = __real_esp_lcd_panel_draw_bitmap(panel, x_start, y_start, x_end, y_end, data);
    fnos_perf_note_stage(FNOS_PERF_SUBMIT, (uint32_t)(esp_timer_get_time() - start));
    return ret;
}

struct display_pipeline_buf *__real_display_bridge_pipeline_wait_free_buf(esp_lv_adapter_display_pipeline_t *pipeline);
struct display_pipeline_buf *__wrap_display_bridge_pipeline_wait_free_buf(esp_lv_adapter_display_pipeline_t *pipeline)
{
    int64_t start = esp_timer_get_time();
    struct display_pipeline_buf *ret = __real_display_bridge_pipeline_wait_free_buf(pipeline);
    fnos_perf_note_stage(FNOS_PERF_FREE_BUFFER, (uint32_t)(esp_timer_get_time() - start));
    return ret;
}

lv_draw_task_t *__real_lv_draw_add_task(lv_layer_t *layer, const lv_area_t *area, lv_draw_task_type_t type);
lv_draw_task_t *__wrap_lv_draw_add_task(lv_layer_t *layer, const lv_area_t *area, lv_draw_task_type_t type)
{
    int64_t start = esp_timer_get_time();
    lv_draw_task_t *ret = __real_lv_draw_add_task(layer, area, type);
    fnos_perf_note_stage(FNOS_PERF_TASK_CREATE, (uint32_t)(esp_timer_get_time() - start));
    return ret;
}

lv_draw_task_t *__real_lv_draw_get_available_task(lv_layer_t *layer, lv_draw_task_t *previous, uint8_t unit);
lv_draw_task_t *__wrap_lv_draw_get_available_task(lv_layer_t *layer, lv_draw_task_t *previous, uint8_t unit)
{
    int64_t start = esp_timer_get_time();
    lv_draw_task_t *ret = __real_lv_draw_get_available_task(layer, previous, unit);
    fnos_perf_note_stage(FNOS_PERF_TASK_PICK, (uint32_t)(esp_timer_get_time() - start));
    return ret;
}

lv_draw_task_t *__real_lv_draw_get_next_available_task(lv_layer_t *layer, lv_draw_task_t *previous, uint8_t unit);
lv_draw_task_t *__wrap_lv_draw_get_next_available_task(lv_layer_t *layer, lv_draw_task_t *previous, uint8_t unit)
{
    int64_t start = esp_timer_get_time();
    lv_draw_task_t *ret = __real_lv_draw_get_next_available_task(layer, previous, unit);
    fnos_perf_note_stage(FNOS_PERF_TASK_PICK, (uint32_t)(esp_timer_get_time() - start));
    return ret;
}

void __real_lv_draw_sw_blend(lv_draw_task_t *task, const lv_draw_sw_blend_dsc_t *dsc);
void __wrap_lv_draw_sw_blend(lv_draw_task_t *task, const lv_draw_sw_blend_dsc_t *dsc)
{
    int64_t start = esp_timer_get_time();
    __real_lv_draw_sw_blend(task, dsc);
    fnos_perf_note_stage(FNOS_PERF_BLEND, (uint32_t)(esp_timer_get_time() - start));
}

void __real_lv_draw_sw_label(lv_draw_task_t *task, const lv_draw_label_dsc_t *dsc, const lv_area_t *area);
void __wrap_lv_draw_sw_label(lv_draw_task_t *task, const lv_draw_label_dsc_t *dsc, const lv_area_t *area)
{
    int64_t start = esp_timer_get_time();
    __real_lv_draw_sw_label(task, dsc, area);
    fnos_perf_note_stage(FNOS_PERF_TEXT, (uint32_t)(esp_timer_get_time() - start));
}

void __real_lv_draw_sw_letter(lv_draw_task_t *task, const lv_draw_letter_dsc_t *dsc, const lv_area_t *area);
void __wrap_lv_draw_sw_letter(lv_draw_task_t *task, const lv_draw_letter_dsc_t *dsc, const lv_area_t *area)
{
    int64_t start = esp_timer_get_time();
    __real_lv_draw_sw_letter(task, dsc, area);
    fnos_perf_note_stage(FNOS_PERF_TEXT, (uint32_t)(esp_timer_get_time() - start));
}
