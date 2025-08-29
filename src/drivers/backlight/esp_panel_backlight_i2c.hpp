/*
 * SPDX-FileCopyrightText: 2023-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <memory>
#include "esp_panel_backlight.hpp"
#include "esp_panel_backlight_i2c_commands.h"

namespace esp_panel::drivers {

/**
 * @brief The I2C backlight driver class
 *
 * This class provides I2C-based backlight control functionality
 */
class BacklightI2C : public Backlight {
public:
    /**
     * @brief The I2C backlight configuration structure
     */
    struct Config {
        esp_panel_backlight_i2c_config_t i2c_config;  ///< I2C configuration
    };

    /**
     * @brief Basic attributes for I2C backlight
     */
    static constexpr BasicAttributes BASIC_ATTRIBUTES_DEFAULT = {
        .type = ESP_PANEL_BACKLIGHT_TYPE_IIC,
        .name = "I2C"
    };

    /**
     * @brief Constructor
     *
     * @param[in] config The I2C backlight configuration
     */
    explicit BacklightI2C(const Config &config);

    /**
     * @brief Destructor
     */
    ~BacklightI2C() override;

    /**
     * @brief Initialize and start the I2C backlight device
     *
     * @return `true` if successful, `false` otherwise
     */
    bool begin() override;

    /**
     * @brief Delete the I2C backlight device and release resources
     *
     * @return `true` if successful, `false` otherwise
     */
    bool del() override;

    /**
     * @brief Set the brightness level
     *
     * @param[in] percent The brightness percentage (0-100)
     *
     * @return `true` if successful, `false` otherwise
     */
    bool setBrightness(int percent) override;

    /**
     * @brief Turn on the I2C backlight
     *
     * @return `true` if successful, `false` otherwise
     */
    bool on();

    /**
     * @brief Turn off the I2C backlight
     *
     * @return `true` if successful, `false` otherwise
     */
    bool off();

private:
    Config _config;  ///< The I2C backlight configuration
    bool _initialized;  ///< Initialization status
};

} // namespace esp_panel::drivers
