#!/usr/bin/env python3
"""Compile the BSP touch probe against stubs and lock the no-touch startup contract."""
import re
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = (root / 'components/esp32_p4_wifi6_touch_lcd_7b/esp32_p4_wifi6_touch_lcd_7b.c').read_text()


def function(name):
    match = re.search(r'^(?:static )?[\w *]+\b' + name + r'\([^;]*?\)\s*\{', source, re.M)
    if not match:
        raise RuntimeError('missing production function: ' + name)
    start = source.index('{', match.start())
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


preamble = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define ESP_OK 0
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define TAG "test"
#define CONFIG_BSP_I2C_CLK_SPEED_HZ 400000
#define BSP_LCD_H_RES 1024
#define BSP_LCD_V_RES 600
#define BSP_LCD_TOUCH_RST 1
#define BSP_LCD_TOUCH_INT 2
#define ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS 0x5d
#define ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP 0x14
#define pdMS_TO_TICKS(ms) (ms)
#define ESP_RETURN_ON_ERROR(x, tag, fmt) do { esp_err_t e_ = (x); if (e_ != ESP_OK) return e_; } while (0)
#define BSP_ERROR_CHECK_RETURN_ERR(x) do { esp_err_t e_ = (x); if (e_ != ESP_OK) return e_; } while (0)
#define BSP_TOUCH_PROBE_ATTEMPTS 3
#define BSP_TOUCH_PROBE_DELAY_MS 50

typedef int esp_err_t;
typedef void *esp_lcd_panel_io_handle_t;
typedef void *esp_lcd_touch_handle_t;

typedef struct {
    struct { int swap_xy, mirror_x, mirror_y; } flags;
} bsp_touch_config_t;

typedef struct {
    int x_max, y_max;
    int rst_gpio_num, int_gpio_num;
    struct { int reset, interrupt; } levels;
    struct { int swap_xy, mirror_x, mirror_y; } flags;
} esp_lcd_touch_config_t;

typedef struct {
    uint8_t dev_addr;
    uint32_t scl_speed_hz;
} esp_lcd_panel_io_i2c_config_t;

#define ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG() \
    ((esp_lcd_panel_io_i2c_config_t){ .dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS })

static void *i2c_handle = NULL;
static int probe_calls, delay_calls, create_calls, io_del_calls, touch_init_rc;
static int probe_ok_at = -1, probe_ok_addr, created_addr;

static esp_err_t bsp_i2c_init(void)
{
    return ESP_OK;
}

static esp_err_t bsp_i2c_device_probe(uint8_t addr)
{
    int index = probe_calls++;
    if (index == probe_ok_at && addr == probe_ok_addr) {
        return ESP_OK;
    }
    return 1;
}

static void vTaskDelay(uint32_t ticks)
{
    (void)ticks;
    delay_calls++;
}

static esp_err_t esp_lcd_new_panel_io_i2c(void *bus, const esp_lcd_panel_io_i2c_config_t *cfg,
                                          esp_lcd_panel_io_handle_t *out)
{
    (void)bus;
    created_addr = cfg->dev_addr;
    *out = (void *)1;
    return ESP_OK;
}

static esp_err_t esp_lcd_touch_new_i2c_gt911(esp_lcd_panel_io_handle_t io,
                                             const esp_lcd_touch_config_t *cfg,
                                             esp_lcd_touch_handle_t *ret_touch)
{
    (void)io;
    (void)cfg;
    create_calls++;
    if (touch_init_rc != ESP_OK) {
        return touch_init_rc;
    }
    *ret_touch = (void *)1;
    return ESP_OK;
}

static esp_err_t esp_lcd_panel_io_del(esp_lcd_panel_io_handle_t io)
{
    (void)io;
    io_del_calls++;
    return ESP_OK;
}

static void reset_fixture(void)
{
    probe_calls = 0;
    delay_calls = 0;
    create_calls = 0;
    io_del_calls = 0;
    touch_init_rc = ESP_OK;
    probe_ok_at = -1;
    probe_ok_addr = 0;
    created_addr = -1;
}
'''

checks = r'''
int main(void)
{
    bsp_touch_config_t cfg = {0};
    esp_lcd_touch_handle_t touch = NULL;

    /* Absent controller: three attempts, two back-off delays, no panel IO created. */
    reset_fixture();
    assert(bsp_touch_new(&cfg, &touch) == ESP_ERR_NOT_FOUND);
    assert(probe_calls == 6);
    assert(delay_calls == 2);
    assert(create_calls == 0);

    /* Backup address appears on the second attempt. */
    reset_fixture();
    probe_ok_at = 3;
    probe_ok_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP;
    assert(bsp_touch_new(&cfg, &touch) == ESP_OK);
    assert(probe_calls == 4);
    assert(delay_calls == 1);
    assert(created_addr == ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP);
    assert(create_calls == 1);
    assert(io_del_calls == 0);

    /* Primary address appears on the third attempt. */
    reset_fixture();
    probe_ok_at = 4;
    probe_ok_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS;
    assert(bsp_touch_new(&cfg, &touch) == ESP_OK);
    assert(probe_calls == 5);
    assert(delay_calls == 2);
    assert(created_addr == ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS);
    assert(io_del_calls == 0);

    /* Touch driver failure releases the panel IO it was given. */
    reset_fixture();
    probe_ok_at = 0;
    probe_ok_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS;
    touch_init_rc = ESP_ERR_NOT_FOUND;
    assert(bsp_touch_new(&cfg, &touch) == ESP_ERR_NOT_FOUND);
    assert(create_calls == 1);
    assert(io_del_calls == 1);

    puts("PASS: touch probe retries, backup address, absent controller and IO cleanup");
    return 0;
}
'''

start = source.index('lv_display_t *bsp_display_start_with_config')
display_start = source[start:]
if 'disp_indev = bsp_display_indev_init(cfg, disp);' not in display_start:
    raise RuntimeError('display start no longer attempts touch without the fatal NULL check')
if 'if (disp_indev == NULL)' not in display_start:
    raise RuntimeError('display start does not tolerate a missing touch input')
if 'BSP_NULL_CHECK(disp_indev' in display_start:
    raise RuntimeError('touch input is still treated as fatal for display startup')

with tempfile.TemporaryDirectory() as tmp:
    src = Path(tmp) / 'main.c'
    exe = Path(tmp) / 'touch_startup'
    src.write_text(preamble + function('bsp_touch_new') + checks)
    build = subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', '-o', str(exe), str(src)],
                           text=True, capture_output=True)
    if build.returncode != 0:
        raise SystemExit('compile failed:\n' + build.stderr)
    run = subprocess.run([str(exe)], text=True, capture_output=True)
    if run.returncode != 0:
        raise SystemExit('run failed:\n' + run.stdout + run.stderr)
    print(run.stdout, end='')
