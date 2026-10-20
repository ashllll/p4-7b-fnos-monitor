# 源文件显式列出，不用 glob：字体是 tools/gen_fonts.sh 生成的，
# glob 在配置阶段只求值一次，新增文件会被静默漏掉（表现为链接期 undefined reference）。
# 增删字体时同步改这里的列表（gen_fonts.sh 会打印需要的行）。
set(PROJ_SRCS
    "fnos_net.c"
    "fnos_wifi_store.c"
    "fnos_wifi_cli.c"
    "fnos_data.c"
    "fnos_snapshot.c"
    "fnos_pair.c"
    "fnos_ui.c"
    # 触控/渲染台架（串口 `bench`）：CONFIG_FNOS_UI_PERF_BENCH=n 时整个文件退化成空实现。
    # 放在这里而不是 main/，是为了能直接量 ui_tick / motion 这条 LVGL 路径。
    "fnos_perf.c"
    "ui_kit/uk.c"
    "ui_kit/uk_number.c"
    "ui_kit/uk_meter.c"
    "ui_kit/uk_spring.c"
    "ui_kit/uk_theme.c"
    "fonts/ui_font_cjk_12.c"
    "fonts/ui_font_cjk_16.c"
    "fonts/ui_font_cjk_20.c"
    "fonts/ui_font_num_20.c"
    "fonts/ui_font_num_32.c"
    "fonts/ui_font_num_44.c"
    "fonts/ui_font_display_64.c"
    "fonts/ui_font_display_96.c"
    "fonts/ui_font_txt_12.c"
    "fonts/ui_font_txt_15.c"
)

idf_component_register(
    SRCS ${PROJ_SRCS}
    INCLUDE_DIRS
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/ui_kit
    PRIV_REQUIRES esp_driver_ppa esp_lcd
    REQUIRES
        esp_wifi
        esp_netif
        esp_event
        esp_http_client
        # 把连接层错误归类成人能看懂的失败原因（esp_tls_errors.h 里的 ESP_ERR_ESP_TLS_*）
        esp-tls
        # 配对：自己算证书 SHA-256、解析 CN/到期日（不用服务器给的指纹字段）
        mbedtls
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

include("${CMAKE_CURRENT_LIST_DIR}/cmake/fnos_chart_math.cmake")
fnos_use_safe_chart_map(__idf_lvgl__lvgl)

if(CONFIG_FNOS_UI_PERF_BENCH)
    target_sources(${COMPONENT_LIB} PRIVATE "fnos_perf_display.c")
    # Use the installed adapter's exact private signatures, without copying layouts.
    target_include_directories(${COMPONENT_LIB} PRIVATE
        $<TARGET_PROPERTY:__idf_espressif__esp_lvgl_adapter,INCLUDE_DIRECTORIES>)
    foreach(probe ppa_do_scale_rotate_mirror display_bridge_dma2d_copy_sync
                  esp_lcd_panel_draw_bitmap display_bridge_pipeline_wait_free_buf)
        target_link_libraries(${COMPONENT_LIB} INTERFACE
            "-Wl,--wrap=${probe},--undefined=__wrap_${probe}")
    endforeach()
    if(CONFIG_FNOS_UI_PERF_DRAW)
        foreach(probe lv_draw_add_task lv_draw_get_available_task lv_draw_get_next_available_task
                      lv_draw_sw_blend lv_draw_sw_label lv_draw_sw_letter)
            target_link_libraries(${COMPONENT_LIB} INTERFACE
                "-Wl,--wrap=${probe},--undefined=__wrap_${probe}")
        endforeach()
    endif()
endif()
