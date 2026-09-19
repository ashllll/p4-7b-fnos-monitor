set(PROJ_SRC ${CMAKE_CURRENT_LIST_DIR})

file(GLOB_RECURSE PROJ_SRCS_C ${PROJ_SRC}/*.c)

idf_component_register(
    SRCS ${PROJ_SRCS_C}
    INCLUDE_DIRS ${PROJ_SRC}
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

# 字体 C 数组（Lato / crisp）沿用上游生成参数，关掉它们触发的告警
target_compile_options(
    ${COMPONENT_LIB}
    PRIVATE
        $<$<COMPILE_LANGUAGE:C>:-Wno-format -Wno-unused-variable>
)
