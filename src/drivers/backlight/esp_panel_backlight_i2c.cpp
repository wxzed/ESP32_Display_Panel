/*
 * SPDX-FileCopyrightText: 2023-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_panel_backlight_i2c.hpp"
#include "utils/esp_panel_utils_log.h"

namespace esp_panel::drivers {



BacklightI2C::BacklightI2C(const Config &config)
    : Backlight(BASIC_ATTRIBUTES_DEFAULT), _config(config), _initialized(false)
{
    ESP_UTILS_LOG_TRACE_ENTER();
    ESP_UTILS_LOGD("Param: config(@%p)", &config);
    ESP_UTILS_LOG_TRACE_EXIT();
}

BacklightI2C::~BacklightI2C()
{
    ESP_UTILS_LOG_TRACE_ENTER();
    del();
    ESP_UTILS_LOG_TRACE_EXIT();
}

bool BacklightI2C::begin()
{
    ESP_UTILS_LOG_TRACE_ENTER();

    if (_initialized) {
        ESP_UTILS_LOGW("Already initialized");
        ESP_UTILS_LOG_TRACE_EXIT();
        return true;
    }

    esp_err_t ret = esp_panel_backlight_i2c_init(&_config.i2c_config);
    if (ret == ESP_OK) {
        _initialized = true;
        setState(State::BEGIN);
        ESP_UTILS_LOGI("I2C backlight initialized successfully");
        ESP_UTILS_LOG_TRACE_EXIT();
        return true;
    } else {
        ESP_UTILS_LOGE("Failed to initialize I2C backlight: %s", esp_err_to_name(ret));
        ESP_UTILS_LOG_TRACE_EXIT();
        return false;
    }
}

bool BacklightI2C::del()
{
    ESP_UTILS_LOG_TRACE_ENTER();

    if (!_initialized) {
        ESP_UTILS_LOGW("Not initialized");
        ESP_UTILS_LOG_TRACE_EXIT();
        return true;
    }

    esp_err_t ret = esp_panel_backlight_i2c_deinit();
    if (ret == ESP_OK) {
        _initialized = false;
        setState(State::DEINIT);
        ESP_UTILS_LOGI("I2C backlight deinitialized successfully");
        ESP_UTILS_LOG_TRACE_EXIT();
        return true;
    } else {
        ESP_UTILS_LOGE("Failed to deinitialize I2C backlight: %s", esp_err_to_name(ret));
        ESP_UTILS_LOG_TRACE_EXIT();
        return false;
    }
}

bool BacklightI2C::setBrightness(int percent)
{
    ESP_UTILS_LOG_TRACE_ENTER();
    ESP_UTILS_LOGD("Param: percent=%d", percent);

    if (!_initialized) {
        ESP_UTILS_LOGE("Not initialized");
        ESP_UTILS_LOG_TRACE_EXIT();
        return false;
    }

    esp_err_t ret = esp_panel_backlight_i2c_set_brightness(percent);
    if (ret == ESP_OK) {
        setBrightnessValue(percent);
        ESP_UTILS_LOG_TRACE_EXIT();
        return true;
    } else {
        ESP_UTILS_LOGE("Failed to set brightness: %s", esp_err_to_name(ret));
        ESP_UTILS_LOG_TRACE_EXIT();
        return false;
    }
}

bool BacklightI2C::on()
{
    ESP_UTILS_LOG_TRACE_ENTER();

    if (!_initialized) {
        ESP_UTILS_LOGE("Not initialized");
        ESP_UTILS_LOG_TRACE_EXIT();
        return false;
    }

    esp_err_t ret = esp_panel_backlight_i2c_set_power(true);
    if (ret == ESP_OK) {
        setBrightnessValue(100);
        ESP_UTILS_LOGI("I2C backlight turned on");
        ESP_UTILS_LOG_TRACE_EXIT();
        return true;
    } else {
        ESP_UTILS_LOGE("Failed to turn on I2C backlight: %s", esp_err_to_name(ret));
        ESP_UTILS_LOG_TRACE_EXIT();
        return false;
    }
}

bool BacklightI2C::off()
{
    ESP_UTILS_LOG_TRACE_ENTER();

    if (!_initialized) {
        ESP_UTILS_LOGE("Not initialized");
        ESP_UTILS_LOG_TRACE_EXIT();
        return false;
    }

    esp_err_t ret = esp_panel_backlight_i2c_set_power(false);
    if (ret == ESP_OK) {
        setBrightnessValue(0);
        ESP_UTILS_LOGI("I2C backlight turned off");
        ESP_UTILS_LOG_TRACE_EXIT();
        return true;
    } else {
        ESP_UTILS_LOGE("Failed to turn off I2C backlight: %s", esp_err_to_name(ret));
        ESP_UTILS_LOG_TRACE_EXIT();
        return false;
    }
}

} // namespace esp_panel::drivers
