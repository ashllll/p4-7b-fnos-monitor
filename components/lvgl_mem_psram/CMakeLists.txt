set(PROJ_SRC ${CMAKE_CURRENT_LIST_DIR})

idf_component_register(
    SRCS "lvgl_mem_psram.c"
    INCLUDE_DIRS ${PROJ_SRC}
    REQUIRES lvgl__lvgl
    # LVGL 静态库里对 lv_*_core 的引用必须能把本组件拉进链接
    WHOLE_ARCHIVE
)
