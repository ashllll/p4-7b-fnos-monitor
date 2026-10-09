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
    "ui_kit/uk.c"
    "ui_kit/uk_number.c"
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
