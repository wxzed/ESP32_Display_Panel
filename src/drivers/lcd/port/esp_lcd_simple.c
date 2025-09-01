/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "../esp_panel_lcd_conf_internal.h"

#if ESP_PANEL_DRIVERS_LCD_ENABLE_SIMPLE

#include "soc/soc_caps.h"

#if SOC_MIPI_DSI_SUPPORTED
#include "esp_check.h"
#include "esp_log.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_vendor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_lcd_simple.h"

#include "utils/esp_panel_utils_log.h"
#include "esp_utils_helpers.h"
#include "esp_panel_lcd_vendor_types.h"

static const char *TAG = "lcd_simple";

typedef struct {
    esp_lcd_panel_io_handle_t io;
    int reset_gpio_num;
    uint8_t madctl_val; // save current value of LCD_CMD_MADCTL register
    uint8_t colmod_val; // save current value of LCD_CMD_COLMOD register
    const esp_panel_lcd_vendor_init_cmd_t *init_cmds;
    uint16_t init_cmds_size;
    uint8_t lane_num;
    struct {
        unsigned int reset_level: 1;
    } flags;
    // To save the original functions of MIPI DPI panel
    esp_err_t (*del)(esp_lcd_panel_t *panel);
    esp_err_t (*init)(esp_lcd_panel_t *panel);
} simple_panel_t;

static esp_err_t panel_simple_del(esp_lcd_panel_t *panel);
static esp_err_t panel_simple_init(esp_lcd_panel_t *panel);
static esp_err_t panel_simple_reset(esp_lcd_panel_t *panel);
static esp_err_t panel_simple_invert_color(esp_lcd_panel_t *panel, bool invert_color_data);
static esp_err_t panel_simple_mirror(esp_lcd_panel_t *panel, bool mirror_x, bool mirror_y);
static esp_err_t panel_simple_disp_on_off(esp_lcd_panel_t *panel, bool on_off);
static esp_err_t panel_simple_sleep(esp_lcd_panel_t *panel, bool sleep);

esp_err_t esp_lcd_new_panel_simple(const esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *panel_dev_config,
                                   esp_lcd_panel_handle_t *ret_panel)
{
    ESP_LOGI(TAG, "version: %d.%d.%d", ESP_LCD_SIMPLE_VER_MAJOR, ESP_LCD_SIMPLE_VER_MINOR, ESP_LCD_SIMPLE_VER_PATCH);
    ESP_RETURN_ON_FALSE(io && panel_dev_config && ret_panel, ESP_ERR_INVALID_ARG, TAG, "invalid arguments");
    esp_panel_lcd_vendor_config_t *vendor_config = (esp_panel_lcd_vendor_config_t *)panel_dev_config->vendor_config;
    ESP_RETURN_ON_FALSE(vendor_config && vendor_config->mipi_config.dpi_config && vendor_config->mipi_config.dsi_bus, ESP_ERR_INVALID_ARG, TAG,
                        "invalid vendor config");

    esp_err_t ret = ESP_OK;
    simple_panel_t *simple = (simple_panel_t *)calloc(1, sizeof(simple_panel_t));
    ESP_RETURN_ON_FALSE(simple, ESP_ERR_NO_MEM, TAG, "no mem for simple panel");

    if (panel_dev_config->reset_gpio_num >= 0) {
        gpio_config_t io_conf = {
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = 1ULL << panel_dev_config->reset_gpio_num,
        };
        ESP_GOTO_ON_ERROR(gpio_config(&io_conf), err, TAG, "configure GPIO for RST line failed");
    }

    switch (panel_dev_config->rgb_ele_order) {
    case LCD_RGB_ELEMENT_ORDER_RGB:
        simple->madctl_val = 0;
        break;
    case LCD_RGB_ELEMENT_ORDER_BGR:
        simple->madctl_val |= LCD_CMD_BGR_BIT;
        break;
    default:
        ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, TAG, "unsupported color space");
        break;
    }

    switch (panel_dev_config->bits_per_pixel) {
    case 16: // RGB565
        simple->colmod_val = 0x55;
        break;
    case 18: // RGB666
        simple->colmod_val = 0x66;
        break;
    case 24: // RGB888
        simple->colmod_val = 0x77;
        break;
    default:
        ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, TAG, "unsupported pixel width");
        break;
    }

    simple->io = io;
    simple->init_cmds = vendor_config->init_cmds;
    simple->init_cmds_size = vendor_config->init_cmds_size;
    simple->lane_num = vendor_config->mipi_config.lane_num;
    simple->reset_gpio_num = panel_dev_config->reset_gpio_num;
    simple->flags.reset_level = panel_dev_config->flags.reset_active_high;

    // Create MIPI DPI panel
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_dpi(vendor_config->mipi_config.dsi_bus, vendor_config->mipi_config.dpi_config, ret_panel), err, TAG,
                      "create MIPI DPI panel failed");
    ESP_LOGD(TAG, "new MIPI DPI panel @%p", *ret_panel);

    // Save the original functions of MIPI DPI panel
    simple->del = (*ret_panel)->del;
    simple->init = (*ret_panel)->init;
    // Overwrite the functions of MIPI DPI panel
    (*ret_panel)->del = panel_simple_del;
    (*ret_panel)->init = panel_simple_init;
    (*ret_panel)->reset = panel_simple_reset;
    (*ret_panel)->mirror = panel_simple_mirror;
    (*ret_panel)->invert_color = panel_simple_invert_color;
    (*ret_panel)->disp_on_off = panel_simple_disp_on_off;
    (*ret_panel)->disp_sleep = panel_simple_sleep;
    (*ret_panel)->user_data = simple;
    ESP_LOGD(TAG, "new simple panel @%p", simple);

    return ESP_OK;

err:
    if (simple) {
        if (panel_dev_config->reset_gpio_num >= 0) {
            gpio_reset_pin(panel_dev_config->reset_gpio_num);
        }
        free(simple);
    }
    return ret;
}

static const esp_panel_lcd_vendor_init_cmd_t vendor_specific_init_default[] = {
    // {cmd, { data }, data_size, delay_ms}
    // No initialization commands for simple driver
};

static esp_err_t panel_simple_del(esp_lcd_panel_t *panel)
{
    simple_panel_t *simple = (simple_panel_t *)panel->user_data;

    if (simple->reset_gpio_num >= 0) {
        gpio_reset_pin(simple->reset_gpio_num);
    }
    // Delete MIPI DPI panel
    simple->del(panel);
    ESP_LOGD(TAG, "del simple panel @%p", simple);
    free(simple);

    return ESP_OK;
}

#endif // SOC_MIPI_DSI_SUPPORTED
#endif // ESP_PANEL_DRIVERS_LCD_ENABLE_SIMPLE

static esp_err_t panel_simple_init(esp_lcd_panel_t *panel)
{
    ESP_LOGI(TAG, "panel_simple_init");
    simple_panel_t *simple = (simple_panel_t *)panel->user_data;
    esp_lcd_panel_io_handle_t io = simple->io;
    const esp_panel_lcd_vendor_init_cmd_t *init_cmds = NULL;
    uint16_t init_cmds_size = 0;
    bool is_cmd_overwritten = false;

    // vendor specific initialization, it can be different between manufacturers
    // should consult the LCD supplier for initialization sequence code
    if (simple->init_cmds) {
        init_cmds = simple->init_cmds;
        init_cmds_size = simple->init_cmds_size;
    } else {
        init_cmds = vendor_specific_init_default;
        init_cmds_size = sizeof(vendor_specific_init_default) / sizeof(esp_panel_lcd_vendor_init_cmd_t);
    }

    for (int i = 0; i < init_cmds_size; i++) {
        // Check if the command has been used or conflicts with the internal
        if (init_cmds[i].data_bytes > 0) {
            switch (init_cmds[i].cmd) {
            case LCD_CMD_MADCTL:
                is_cmd_overwritten = true;
                simple->madctl_val = ((uint8_t *)init_cmds[i].data)[0];
                break;
            case LCD_CMD_COLMOD:
                is_cmd_overwritten = true;
                break;
            default:
                is_cmd_overwritten = false;
                break;
            }

            if (is_cmd_overwritten) {
                is_cmd_overwritten = false;
                ESP_LOGW(TAG, "The %02Xh command has been used and will be overwritten by external initialization sequence",
                         init_cmds[i].cmd);
            }
        }

        // Send command - but for simple driver, we skip this
        // ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, init_cmds[i].cmd, init_cmds[i].data, init_cmds[i].data_bytes), TAG, "send command failed");
        ESP_LOGI(TAG, "Skipping command 0x%02X for simple driver", init_cmds[i].cmd);

        if (init_cmds[i].delay_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(init_cmds[i].delay_ms));
        }
    }

    ESP_LOGI(TAG, "Simple LCD panel init completed - no commands sent");

    ESP_RETURN_ON_ERROR(simple->init(panel), TAG, "init MIPI DPI panel failed");

    return ESP_OK;
}

static esp_err_t panel_simple_reset(esp_lcd_panel_t *panel)
{
    simple_panel_t *simple = (simple_panel_t *)panel->user_data;

    if (simple->reset_gpio_num >= 0) {
        gpio_set_level(simple->reset_gpio_num, !simple->flags.reset_level);
        esp_rom_delay_us(10 * 1000); // 10ms
        gpio_set_level(simple->reset_gpio_num, simple->flags.reset_level);
        esp_rom_delay_us(10 * 1000); // 10ms
        ESP_LOGI(TAG, "Simple LCD panel reset");
    }

    return ESP_OK;
}

static esp_err_t panel_simple_invert_color(esp_lcd_panel_t *panel, bool invert_color_data)
{
    simple_panel_t *simple = (simple_panel_t *)panel->user_data;

    // This driver does not support color inversion
    ESP_LOGI(TAG, "Color inversion called (invert=%d) - not supported, returning success", invert_color_data);
    return ESP_OK;
}

static esp_err_t panel_simple_mirror(esp_lcd_panel_t *panel, bool mirror_x, bool mirror_y)
{
    simple_panel_t *simple = (simple_panel_t *)panel->user_data;

    // This driver does not support mirroring
    ESP_LOGI(TAG, "Mirror called (x=%d, y=%d) - not supported, returning success", mirror_x, mirror_y);
    return ESP_OK;
}

static esp_err_t panel_simple_disp_on_off(esp_lcd_panel_t *panel, bool on_off)
{
    simple_panel_t *simple = (simple_panel_t *)panel->user_data;

    // This driver does not support display on/off control
    ESP_LOGI(TAG, "Display on/off control called (on_off=%d) - not supported, returning success", on_off);
    return ESP_OK;
}

static esp_err_t panel_simple_sleep(esp_lcd_panel_t *panel, bool sleep)
{
    simple_panel_t *simple = (simple_panel_t *)panel->user_data;

    // This driver does not support sleep control
    ESP_LOGI(TAG, "Sleep control called (sleep=%d) - not supported, returning success", sleep);
    return ESP_OK;
}


