/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

  #include "../esp_panel_lcd_conf_internal.h"
 
 
 #if ESP_PANEL_DRIVERS_LCD_ENABLE_NODSICONF
 
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
 #include "esp_lcd_nodsiconf.h"
 
 #include "utils/esp_panel_utils_log.h"
#include "esp_utils_helpers.h"
#include "esp_panel_lcd_vendor_types.h"

 #define NODSICONF_CMD_GS_BIT (1 << 0)
 #define NODSICONF_CMD_SS_BIT (1 << 1)

 typedef struct {
     esp_lcd_panel_io_handle_t io;
     int reset_gpio_num;
     uint8_t madctl_val; // save current value of LCD_CMD_MADCTL register
     uint8_t colmod_val; // save surrent value of LCD_CMD_COLMOD register
     const esp_panel_lcd_vendor_init_cmd_t *init_cmds;
     uint16_t init_cmds_size;
     uint8_t lane_num;
     struct {
         unsigned int reset_level: 1;
     } flags;
     // To save the original functions of MIPI DPI panel
     esp_err_t (*del)(esp_lcd_panel_t *panel);
     esp_err_t (*init)(esp_lcd_panel_t *panel);
 } nodsiconf_panel_t;
 
 static const char *TAG = "nodsiconf";
 
 static esp_err_t panel_nodsiconf_del(esp_lcd_panel_t *panel);
 static esp_err_t panel_nodsiconf_init(esp_lcd_panel_t *panel);
 static esp_err_t panel_nodsiconf_reset(esp_lcd_panel_t *panel);
 static esp_err_t panel_nodsiconf_invert_color(esp_lcd_panel_t *panel, bool invert_color_data);
 static esp_err_t panel_nodsiconf_mirror(esp_lcd_panel_t *panel, bool mirror_x, bool mirror_y);
 static esp_err_t panel_nodsiconf_disp_on_off(esp_lcd_panel_t *panel, bool on_off);
 static esp_err_t panel_nodsiconf_sleep(esp_lcd_panel_t *panel, bool sleep);
 
 esp_err_t esp_lcd_new_panel_nodsiconf(const esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *panel_dev_config,
                                      esp_lcd_panel_handle_t *ret_panel)
 {
     ESP_LOGI(
         TAG, "version: %d.%d.%d", ESP_LCD_NODSICONF_VER_MAJOR, ESP_LCD_NODSICONF_VER_MINOR, ESP_LCD_NODSICONF_VER_PATCH
     );
     ESP_RETURN_ON_FALSE(io && panel_dev_config && ret_panel, ESP_ERR_INVALID_ARG, TAG, "invalid arguments");
     esp_panel_lcd_vendor_config_t *vendor_config = (esp_panel_lcd_vendor_config_t *)panel_dev_config->vendor_config;
     ESP_RETURN_ON_FALSE(vendor_config && vendor_config->mipi_config.dpi_config && vendor_config->mipi_config.dsi_bus, ESP_ERR_INVALID_ARG, TAG,
                         "invalid vendor config");
 
     esp_err_t ret = ESP_OK;
     nodsiconf_panel_t *nodsiconf = (nodsiconf_panel_t *)calloc(1, sizeof(nodsiconf_panel_t));
     ESP_RETURN_ON_FALSE(nodsiconf, ESP_ERR_NO_MEM, TAG, "no mem for nodsiconf panel");
 
     if (panel_dev_config->reset_gpio_num >= 0) {
         gpio_config_t io_conf = {
             .mode = GPIO_MODE_OUTPUT,
             .pin_bit_mask = 1ULL << panel_dev_config->reset_gpio_num,
         };
         ESP_GOTO_ON_ERROR(gpio_config(&io_conf), err, TAG, "configure GPIO for RST line failed");
     }
 
     switch (panel_dev_config->rgb_ele_order) {
     case LCD_RGB_ELEMENT_ORDER_RGB:
         nodsiconf->madctl_val = 0;
         break;
     case LCD_RGB_ELEMENT_ORDER_BGR:
         nodsiconf->madctl_val |= LCD_CMD_BGR_BIT;
         break;
     default:
         ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, TAG, "unsupported color space");
         break;
     }
 
     switch (panel_dev_config->bits_per_pixel) {
     case 16: // RGB565
         nodsiconf->colmod_val = 0x55;
         break;
     case 18: // RGB666
         nodsiconf->colmod_val = 0x66;
         break;
     case 24: // RGB888
         nodsiconf->colmod_val = 0x77;
         break;
     default:
         ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, TAG, "unsupported pixel width");
         break;
     }
 
     nodsiconf->io = io;
     nodsiconf->init_cmds = vendor_config->init_cmds;
     nodsiconf->init_cmds_size = vendor_config->init_cmds_size;
     nodsiconf->lane_num = vendor_config->mipi_config.lane_num;
     nodsiconf->reset_gpio_num = panel_dev_config->reset_gpio_num;
         nodsiconf->flags.reset_level = panel_dev_config->flags.reset_active_high;


     // Create MIPI DPI panel
     ESP_GOTO_ON_ERROR(esp_lcd_new_panel_dpi(vendor_config->mipi_config.dsi_bus, vendor_config->mipi_config.dpi_config, ret_panel), err, TAG,
                       "create MIPI DPI panel failed");
     ESP_LOGD(TAG, "new MIPI DPI panel @%p", *ret_panel);
 
     // Save the original functions of MIPI DPI panel
     nodsiconf->del = (*ret_panel)->del;
     nodsiconf->init = (*ret_panel)->init;
     // Overwrite the functions of MIPI DPI panel
     (*ret_panel)->del = panel_nodsiconf_del;
     (*ret_panel)->init = panel_nodsiconf_init;
     (*ret_panel)->reset = panel_nodsiconf_reset;
     (*ret_panel)->mirror = panel_nodsiconf_mirror;
     (*ret_panel)->invert_color = panel_nodsiconf_invert_color;
     (*ret_panel)->disp_on_off = panel_nodsiconf_disp_on_off;
     (*ret_panel)->disp_sleep = panel_nodsiconf_sleep;
     (*ret_panel)->user_data = nodsiconf;
     ESP_LOGD(TAG, "new nodsiconf panel @%p", nodsiconf);
 
     return ESP_OK;
 
 err:
     if (nodsiconf) {
         if (panel_dev_config->reset_gpio_num >= 0) {
             gpio_reset_pin(panel_dev_config->reset_gpio_num);
         }
         free(nodsiconf);
     }
     return ret;
 }
 
 static const esp_panel_lcd_vendor_init_cmd_t vendor_specific_init_default[] = {
     // {cmd, { data }, data_size, delay_ms}
     //{0x11, (uint8_t[]){0x00}, 1, 120},
     //{0x29, (uint8_t[]){0x00}, 1, 20},
 
 };
 
 static esp_err_t panel_nodsiconf_del(esp_lcd_panel_t *panel)
 {
     nodsiconf_panel_t *nodsiconf = (nodsiconf_panel_t *)panel->user_data;
 
     if (nodsiconf->reset_gpio_num >= 0) {
         gpio_reset_pin(nodsiconf->reset_gpio_num);
     }
     // Delete MIPI DPI panel
     nodsiconf->del(panel);
     ESP_LOGD(TAG, "del nodsiconf panel @%p", nodsiconf);
     free(nodsiconf);
 
     return ESP_OK;
 }
 
 static esp_err_t panel_nodsiconf_init(esp_lcd_panel_t *panel)
 {
    ESP_LOGI(TAG, "panel_nodsiconf_init");
     nodsiconf_panel_t *nodsiconf = (nodsiconf_panel_t *)panel->user_data;
     esp_lcd_panel_io_handle_t io = nodsiconf->io;
     const esp_panel_lcd_vendor_init_cmd_t *init_cmds = NULL;
     uint16_t init_cmds_size = 0;
     bool is_cmd_overwritten = false;
     /*
     ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_MADCTL, (uint8_t[]) {
        nodsiconf->madctl_val,
     }, 1), TAG, "send command failed");
    */
     // vendor specific initialization, it can be different between manufacturers
     // should consult the LCD supplier for initialization sequence code
     if (nodsiconf->init_cmds) {
         init_cmds = nodsiconf->init_cmds;
         init_cmds_size = nodsiconf->init_cmds_size;
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
                 nodsiconf->madctl_val = ((uint8_t *)init_cmds[i].data)[0];
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
 
         // Send command
         //ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, init_cmds[i].cmd, init_cmds[i].data, init_cmds[i].data_bytes), TAG, "send command failed");
         vTaskDelay(pdMS_TO_TICKS(init_cmds[i].delay_ms));
     }
     ESP_LOGD(TAG, "send init commands success");
 
     ESP_RETURN_ON_ERROR(nodsiconf->init(panel), TAG, "init MIPI DPI panel failed");
 
     return ESP_OK;
 }
 
 static esp_err_t panel_nodsiconf_reset(esp_lcd_panel_t *panel)
 {
    ESP_LOGI(TAG, "panel_nodsiconf_reset");
     nodsiconf_panel_t *nodsiconf = (nodsiconf_panel_t *)panel->user_data;
     esp_lcd_panel_io_handle_t io = nodsiconf->io;
 
     // Perform hardware reset
     if (nodsiconf->reset_gpio_num >= 0) {
         gpio_set_level(nodsiconf->reset_gpio_num, nodsiconf->flags.reset_level);
         vTaskDelay(pdMS_TO_TICKS(10));
         gpio_set_level(nodsiconf->reset_gpio_num, !nodsiconf->flags.reset_level);
         vTaskDelay(pdMS_TO_TICKS(10));
     } else if (io) { // Perform software reset
         //ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_SWRESET, NULL, 0), TAG, "send command failed");
         vTaskDelay(pdMS_TO_TICKS(20));
     }
 
     return ESP_OK;
 }
 
 static esp_err_t panel_nodsiconf_invert_color(esp_lcd_panel_t *panel, bool invert_color_data)
 {
    ESP_LOGI(TAG, "panel_nodsiconf_invert_color");
     nodsiconf_panel_t *nodsiconf = (nodsiconf_panel_t *)panel->user_data;
     esp_lcd_panel_io_handle_t io = nodsiconf->io;
     uint8_t command = 0;
 
     ESP_RETURN_ON_FALSE(io, ESP_ERR_INVALID_STATE, TAG, "invalid panel IO");
 
     if (invert_color_data) {
         command = LCD_CMD_INVON;
     } else {
         command = LCD_CMD_INVOFF;
     }
     //ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, command, NULL, 0), TAG, "send command failed");
 
     return ESP_OK;
 }
 
 static esp_err_t panel_nodsiconf_mirror(esp_lcd_panel_t *panel, bool mirror_x, bool mirror_y)
 {
    ESP_LOGI(TAG, "panel_nodsiconf_mirror");
     nodsiconf_panel_t *nodsiconf = (nodsiconf_panel_t *)panel->user_data;
     esp_lcd_panel_io_handle_t io = nodsiconf->io;
     uint8_t madctl_val = nodsiconf->madctl_val;
 
     ESP_RETURN_ON_FALSE(io, ESP_ERR_INVALID_STATE, TAG, "invalid panel IO");
 
     // Control mirror through LCD command
     if (mirror_x) {
         madctl_val |= NODSICONF_CMD_GS_BIT;
     } else {
         madctl_val &= ~NODSICONF_CMD_GS_BIT;
     }
     if (mirror_y) {
         madctl_val |= NODSICONF_CMD_SS_BIT;
     } else {
         madctl_val &= ~NODSICONF_CMD_SS_BIT;
     }
     /*
     ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_MADCTL, (uint8_t []) {
         madctl_val
     }, 1), TAG, "send command failed");
     */
     nodsiconf->madctl_val = madctl_val;
     ESP_LOGI(TAG, "madctl_val: 0x%X", madctl_val);
     return ESP_OK;
 }
 
 static esp_err_t panel_nodsiconf_disp_on_off(esp_lcd_panel_t *panel, bool on_off)
 {
    ESP_LOGI(TAG, "panel_nodsiconf_disp_on_off");
     nodsiconf_panel_t *nodsiconf = (nodsiconf_panel_t *)panel->user_data;
     esp_lcd_panel_io_handle_t io = nodsiconf->io;
     int command = 0;
 
     if (on_off) {
         command = LCD_CMD_DISPON;
     } else {
         command = LCD_CMD_DISPOFF;
     }
     //ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, command, NULL, 0), TAG, "send command failed");
     return ESP_OK;
 }
 
 static esp_err_t panel_nodsiconf_sleep(esp_lcd_panel_t *panel, bool sleep)
 {
    ESP_LOGI(TAG, "panel_nodsiconf_sleep");
     nodsiconf_panel_t *nodsiconf = (nodsiconf_panel_t *)panel->user_data;
     esp_lcd_panel_io_handle_t io = nodsiconf->io;
     int command = 0;
 
     if (sleep) {
         command = LCD_CMD_SLPIN;
     } else {
         command = LCD_CMD_SLPOUT;
     }
     //ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, command, NULL, 0), TAG, "send command failed");
     vTaskDelay(pdMS_TO_TICKS(100));
 
     return ESP_OK;
 }
 #endif  // SOC_MIPI_DSI_SUPPORTED
 
 #endif // ESP_PANEL_DRIVERS_LCD_ENABLE_NODSICONF
 