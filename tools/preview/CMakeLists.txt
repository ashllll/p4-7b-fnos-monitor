cmake_minimum_required(VERSION 3.16)
project(fnos_ui_preview C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

get_filename_component(REPO "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(COMP "${REPO}/components/fnos_monitor")
# Only export public UI configuration to the preview, never network credentials.
set(UI_DEVICE_CONFIG "${REPO}/build/config/sdkconfig.h")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${UI_DEVICE_CONFIG}")
file(STRINGS "${UI_DEVICE_CONFIG}" UI_CONFIG REGEX "^#define CONFIG_FNOS_(UI_|CHART_WINDOW )")
string(REPLACE ";" "\n" UI_CONFIG "${UI_CONFIG}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/ui_config_host.h" "${UI_CONFIG}\n")


# LVGL 桌面版 CMake 用的名字是 CONFIG_LV_BUILD_*（不是 LV_BUILD_*）
set(CONFIG_LV_BUILD_DEMOS    OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_TESTS    OFF CACHE BOOL "" FORCE)

# 必须在 add_subdirectory(lvgl) 之前：让 LVGL 库自身也用设备端 Kconfig 编译。
# 只给 preview target 加 -include 是个陷阱——lvgl 的 .c 会退回空 lv_conf.h 的默认值
# （LV_USE_STDLIB_MALLOC=LV_STDLIB_BUILTIN，64KB TLSF 池），与设备端分配器行为不一致；
# 池一耗尽 lv_array_resize 就拿到 NULL，撞上 LVGL 默认的 LV_ASSERT_HANDLER=while(1); 静默死循环。
# 只对 C 语言注入：LVGL 里还有 .S（helium blend）汇编文件，吃不下 C 头
add_compile_options(
  $<$<COMPILE_LANGUAGE:C>:-include>
  $<$<COMPILE_LANGUAGE:C>:${CMAKE_CURRENT_SOURCE_DIR}/lv_kconfig_host.h>
)

add_subdirectory("${REPO}/managed_components/lvgl__lvgl" lvgl)

file(GLOB FONT_SRC "${COMP}/fonts/*.c")

add_executable(preview
  preview.c
  "${COMP}/fnos_snapshot.c"
  "${REPO}/managed_components/espressif__cjson/cJSON/cJSON.c"
  "${COMP}/fnos_ui.c"
  "${COMP}/ui_kit/uk.c"
  "${COMP}/ui_kit/uk_theme.c"
  ${FONT_SRC}
)

# stub/ 必须排在前面：它顶掉 fnos_config.h（真实凭据）并补 sdkconfig.h / esp_*.h
target_include_directories(preview PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/stub"
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${COMP}"
  "${COMP}/ui_kit"
  "${REPO}/managed_components/espressif__cjson/cJSON"
)

# 设备端真实 Kconfig（CONFIG_LV_*）已由上面的 add_compile_options 全局注入
target_compile_options(preview PRIVATE
  -Wall -Wno-unused-parameter
)
target_compile_definitions(preview PRIVATE FNOS_UI_TESTING=1)
target_link_libraries(preview PRIVATE lvgl m)
option(FNOS_PREVIEW_SANITIZE "Check UI ownership and injected allocation failures" OFF)
if(FNOS_PREVIEW_SANITIZE)
  target_compile_options(preview PRIVATE -UNDEBUG -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(preview PRIVATE -fsanitize=address,undefined)
endif()

# Sanitize the ownership layer independently of board/network stubs.
add_executable(snapshot_check snapshot_check.c
  "${COMP}/fnos_snapshot.c"
  "${REPO}/managed_components/espressif__cjson/cJSON/cJSON.c")
target_include_directories(snapshot_check PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/stub"
  "${COMP}" "${REPO}/managed_components/espressif__cjson/cJSON")
target_compile_options(snapshot_check PRIVATE -UNDEBUG -fsanitize=address,undefined -fno-omit-frame-pointer)
target_link_options(snapshot_check PRIVATE -fsanitize=address,undefined)
