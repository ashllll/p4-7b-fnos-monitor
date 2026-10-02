# 源文件显式列出，不用 glob：字体是 tools/gen_fonts.sh 生成的，
# glob 在配置阶段只求值一次，新增文件会被静默漏掉（表现为链接期 undefined reference）。
# 增删字体时同步改这里的列表（gen_fonts.sh 会打印需要的行）。
set(PROJ_SRCS
    "fnos_net.c"
    "fnos_data.c"
    "fnos_ui.c"
    "kk_ui/kk_rect.c"
    "kk_ui/kk_widgets.c"
    "ui/FnosDashboardController.c"
    "ui/FnosDashboardViewAnim.c"
    "ui/Generated/FnosDashboard/fnos_dash_view.generated.c"
    "ui/Generated/FnosDashboard/fnos_dash_store.generated.c"
    "ui/Generated/FnosDashboard/fnos_dash_binder.generated.c"
    "fonts/ui_font_cjk_13.c"
    "fonts/ui_font_cjk_17.c"
    "fonts/ui_font_cjk_24.c"
    "fonts/ui_font_cjk_40.c"
    "fonts/ui_font_num_17.c"
    "fonts/ui_font_num_28.c"
    "fonts/ui_font_num_44.c"
    "fonts/ui_font_num_56.c"
    "fonts/ui_font_txt_13.c"
    "fonts/ui_font_txt_15.c"
)

idf_component_register(
    SRCS ${PROJ_SRCS}
    INCLUDE_DIRS
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/kk_ui
        ${CMAKE_CURRENT_LIST_DIR}/ui
        ${CMAKE_CURRENT_LIST_DIR}/ui/Generated/FnosDashboard
    REQUIRES
        esp_wifi
        esp_netif
        esp_event
        esp_http_client
        esp_timer
        lwip
        nvs_flash
        espressif__cjson
        # 面板：屏幕/触摸由 BSP + esp_lvgl_adapter 提供，本组件只画 UI
        esp32_p4_wifi6_touch_lcd_7b
        espressif__esp_lvgl_adapter
        lvgl__lvgl
)

target_compile_options(
    ${COMPONENT_LIB}
    PRIVATE
        $<$<COMPILE_LANGUAGE:C>:-Wno-format -Wno-unused-variable>
)
