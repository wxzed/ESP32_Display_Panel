/*
 * SPDX-FileCopyrightText: 2023-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "driver/i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief I2C backlight command structure
 */
typedef struct {
    uint8_t command;    ///< I2C command
    uint8_t data;       ///< Command data
    uint32_t delay_ms;  ///< Delay after command in milliseconds
} esp_panel_backlight_i2c_cmd_t;

/**
 * @brief I2C backlight configuration structure
 */
typedef struct {
    i2c_port_t i2c_port;                    ///< I2C port number
    uint8_t i2c_addr;                       ///< I2C device address
    int sda_pin;                           ///< SDA pin number
    int scl_pin;                           ///< SCL pin number
    uint32_t i2c_freq;                     ///< I2C frequency in Hz
    uint8_t brightness_cmd;                ///< Brightness control command
    uint8_t power_cmd;                     ///< Power control command
    uint8_t power_on_value;                ///< Power on value
    uint8_t power_off_value;               ///< Power off value
    int max_brightness;                    ///< Maximum brightness value
    const esp_panel_backlight_i2c_cmd_t *init_sequence;  ///< Initialization command sequence
    int init_sequence_len;                 ///< Length of initialization sequence
} esp_panel_backlight_i2c_config_t;

/**
 * @brief Initialize I2C backlight with configuration
 *
 * @param[in] config I2C backlight configuration
 * @return ESP_OK on success, otherwise error code
 */
esp_err_t esp_panel_backlight_i2c_init(const esp_panel_backlight_i2c_config_t *config);

/**
 * @brief Deinitialize I2C backlight
 *
 * @return ESP_OK on success, otherwise error code
 */
esp_err_t esp_panel_backlight_i2c_deinit(void);

/**
 * @brief Set backlight brightness
 *
 * @param[in] percent Brightness percentage (0-100)
 * @return ESP_OK on success, otherwise error code
 */
esp_err_t esp_panel_backlight_i2c_set_brightness(int percent);

/**
 * @brief Set backlight power state
 *
 * @param[in] on true to turn on, false to turn off
 * @return ESP_OK on success, otherwise error code
 */
esp_err_t esp_panel_backlight_i2c_set_power(bool on);

#ifdef __cplusplus
}
#endif
