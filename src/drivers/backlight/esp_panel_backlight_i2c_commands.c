/*
 * SPDX-FileCopyrightText: 2023-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_panel_backlight_i2c_commands.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "backlight_i2c";

// Global configuration storage
static esp_panel_backlight_i2c_config_t g_i2c_config = {0};
static bool g_i2c_initialized = false;

esp_err_t esp_panel_backlight_i2c_init(const esp_panel_backlight_i2c_config_t *config)
{
    ESP_RETURN_ON_FALSE(config, ESP_ERR_INVALID_ARG, TAG, "Invalid config");
    ESP_RETURN_ON_FALSE(config->init_sequence, ESP_ERR_INVALID_ARG, TAG, "Invalid init sequence");
    ESP_RETURN_ON_FALSE(config->init_sequence_len > 0, ESP_ERR_INVALID_ARG, TAG, "Invalid init sequence length");

    // Check if I2C driver is already installed by trying a simple I2C operation
    // We'll use a dummy write to test if I2C is working
    uint8_t dummy_data[1] = {0};
    esp_err_t ret = i2c_master_write_to_device(config->i2c_port, 0x00, dummy_data, 1, pdMS_TO_TICKS(10));
    if (ret == ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "I2C driver not installed on port %d. Please ensure I2C is initialized by touch or other component first", config->i2c_port);
        return ret;
    }
    // Other errors (like timeout) are expected since we're writing to a dummy address
    ESP_LOGI(TAG, "I2C driver is available on port %d", config->i2c_port);

    // Store configuration
    memcpy(&g_i2c_config, config, sizeof(esp_panel_backlight_i2c_config_t));
    g_i2c_initialized = true;

    ESP_LOGI(TAG, "I2C backlight initialized on port %d, addr 0x%02X", config->i2c_port, config->i2c_addr);
    ESP_LOGI(TAG, "I2C configuration: SDA=%d, SCL=%d, freq=%luHz", config->sda_pin, config->scl_pin, (unsigned long)config->i2c_freq);
    ESP_LOGI(TAG, "Backlight commands: brightness_cmd=0x%02X, power_cmd=0x%02X", config->brightness_cmd, config->power_cmd);
    ESP_LOGI(TAG, "Power values: on=0x%02X, off=0x%02X, max_brightness=%d", config->power_on_value, config->power_off_value, config->max_brightness);

    // Execute initialization sequence
    ESP_LOGI(TAG, "Starting I2C backlight initialization sequence (%d commands)", config->init_sequence_len);
    for (int i = 0; i < config->init_sequence_len; i++) {
        const esp_panel_backlight_i2c_cmd_t *cmd = &config->init_sequence[i];
        
        ESP_LOGI(TAG, "Sending init command %d/%d: cmd=0x%02X, data=0x%02X, delay=%lums", 
                 i + 1, config->init_sequence_len, cmd->command, cmd->data, (unsigned long)cmd->delay_ms);
        
        // Create write buffer with command and data
        uint8_t write_buf[2] = {cmd->command, cmd->data};
        ret = i2c_master_write_to_device(config->i2c_port, config->i2c_addr, 
                                       write_buf, sizeof(write_buf), pdMS_TO_TICKS(100));
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to send init command %d: %s", i, esp_err_to_name(ret));
            return ret;
        }
        
        ESP_LOGI(TAG, "Successfully sent init command %d/%d", i + 1, config->init_sequence_len);
        
        if (cmd->delay_ms > 0) {
            ESP_LOGI(TAG, "Waiting %lums after command %d", (unsigned long)cmd->delay_ms, i + 1);
            vTaskDelay(pdMS_TO_TICKS(cmd->delay_ms));
        }
    }

    ESP_LOGI(TAG, "I2C backlight initialization sequence completed");
    return ESP_OK;
}

esp_err_t esp_panel_backlight_i2c_deinit(void)
{
    if (!g_i2c_initialized) {
        return ESP_OK;
    }

    // Don't delete I2C driver as it might be used by other components (like touch)
    // Just clear our internal state
    g_i2c_initialized = false;
    ESP_LOGI(TAG, "I2C backlight deinitialized (I2C driver left intact for other components)");
    return ESP_OK;
}

esp_err_t esp_panel_backlight_i2c_set_brightness(int percent)
{
    ESP_RETURN_ON_FALSE(g_i2c_initialized, ESP_ERR_INVALID_STATE, TAG, "I2C backlight not initialized");
    ESP_RETURN_ON_FALSE(percent >= 0 && percent <= 100, ESP_ERR_INVALID_ARG, TAG, "Invalid brightness percent");

    // Convert percent to brightness value
    int brightness_value = (percent * g_i2c_config.max_brightness) / 100;
    
    ESP_LOGI(TAG, "Setting brightness: %d%% -> value: %d (max: %d)", percent, brightness_value, g_i2c_config.max_brightness);
    ESP_LOGI(TAG, "Sending I2C command: cmd=0x%02X, data=0x%02X", g_i2c_config.brightness_cmd, (uint8_t)brightness_value);
    
    // Send brightness command
    uint8_t write_buf[2] = {g_i2c_config.brightness_cmd, (uint8_t)brightness_value};
    esp_err_t ret = i2c_master_write_to_device(g_i2c_config.i2c_port, g_i2c_config.i2c_addr, 
                                             write_buf, sizeof(write_buf), pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set brightness: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Successfully set brightness to %d%% (value: %d)", percent, brightness_value);
    return ESP_OK;
}

esp_err_t esp_panel_backlight_i2c_set_power(bool on)
{
    ESP_RETURN_ON_FALSE(g_i2c_initialized, ESP_ERR_INVALID_STATE, TAG, "I2C backlight not initialized");

    // Send power command
    uint8_t data = on ? g_i2c_config.power_on_value : g_i2c_config.power_off_value;
    uint8_t write_buf[2] = {g_i2c_config.power_cmd, data};
    
    ESP_LOGI(TAG, "Setting power: %s -> value: 0x%02X", on ? "ON" : "OFF", data);
    ESP_LOGI(TAG, "Sending I2C command: cmd=0x%02X, data=0x%02X", g_i2c_config.power_cmd, data);
    
    esp_err_t ret = i2c_master_write_to_device(g_i2c_config.i2c_port, g_i2c_config.i2c_addr, 
                                             write_buf, sizeof(write_buf), pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set power: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Successfully set power to %s", on ? "ON" : "OFF");
    return ESP_OK;
}
