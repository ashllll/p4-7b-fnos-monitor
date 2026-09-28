# 源文件显式列出，不用 glob（glob 在配置阶段只求值一次，新增文件会被静默漏掉）。
set(PROJ_SRCS
    "fnos_net.c"
    "fnos_data.c"
    "fnos_ui.c"
    "fnos_view.c"
    "fonts/ui_font_cjk_15.c"
    "fonts/ui_font_cjk_20.c"
    "fonts/ui_font_cjk_24.c"
    "fonts/ui_font_cjk_40.c"
    "fonts/ui_font_mono_20.c"
    "fonts/ui_font_mono_34.c"
    "fonts/ui_font_mono_52.c"
    "fonts/ui_font_mono_84.c"
    "fonts/ui_font_sans_15.c"
    "fonts/ui_font_sans_20.c"
    "fonts/ui_font_sans_24.c"
)

idf_component_register(
    SRCS ${PROJ_SRCS}
    INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR}
    REQUIRES
        esp_wifi esp_netif esp_event esp_http_client esp_timer lwip nvs_flash
        espressif__cjson esp32_p4_wifi6_touch_lcd_7b espressif__esp_lvgl_adapter lvgl__lvgl
)

target_compile_options(${COMPONENT_LIB} PRIVATE $<$<COMPILE_LANGUAGE:C>:-Wno-format -Wno-unused-variable>)
