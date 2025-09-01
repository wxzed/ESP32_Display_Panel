/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_panel_lcd_conf_internal.h"
#if ESP_PANEL_DRIVERS_LCD_ENABLE_SIMPLE

#include "soc/soc_caps.h"
#include "utils/esp_panel_utils_log.h"
#include "port/esp_lcd_simple.h"
#include "esp_panel_lcd_simple.hpp"

namespace esp_panel::drivers {

// *INDENT-OFF*
const LCD::BasicBusSpecificationMap LCD_SIMPLE::_bus_specifications = {
    {
        ESP_PANEL_BUS_TYPE_MIPI_DSI, BasicBusSpecification{
            .color_bits = (1U << BasicBusSpecification::COLOR_BITS_RGB565_16) |
                          (1U << BasicBusSpecification::COLOR_BITS_RGB666_18) |
                          (1U << BasicBusSpecification::COLOR_BITS_RGB888_24),
            .functions = (1U << BasicBusSpecification::FUNC_INVERT_COLOR) |
                         (1U << BasicBusSpecification::FUNC_MIRROR_X) |
                         (1U << BasicBusSpecification::FUNC_MIRROR_Y) |
                         (1U << BasicBusSpecification::FUNC_DISPLAY_ON_OFF),
        },
    },
};
// *INDENT-ON*

LCD_SIMPLE::~LCD_SIMPLE()
{
    ESP_UTILS_LOG_TRACE_ENTER_WITH_THIS();

    ESP_UTILS_CHECK_FALSE_EXIT(del(), "Delete failed");

    ESP_UTILS_LOG_TRACE_EXIT_WITH_THIS();
}

bool LCD_SIMPLE::init()
{
    ESP_UTILS_LOG_TRACE_ENTER_WITH_THIS();

    ESP_UTILS_CHECK_FALSE_RETURN(!isOverState(State::INIT), false, "Already initialized");

#if ESP_PANEL_DRIVERS_BUS_ENABLE_MIPI_DSI
    // Process the device on initialization
    ESP_UTILS_CHECK_FALSE_RETURN(processDeviceOnInit(_bus_specifications), false, "Process device on init failed");

    // Create refresh panel - this driver skips initialization commands
    ESP_UTILS_CHECK_ERROR_RETURN(
        esp_lcd_new_panel_simple(
            getBus()->getControlPanelHandle(), getConfig().getDeviceFullConfig(), &refresh_panel
        ), false, "Create refresh panel failed"
    );
    ESP_UTILS_LOGD("Create simple refresh panel(@%p) - no initialization commands sent", refresh_panel);
#else
    ESP_UTILS_CHECK_FALSE_RETURN(false, false, "MIPI-DSI is not supported");
#endif // ESP_PANEL_DRIVERS_BUS_ENABLE_MIPI_DSI

    setState(State::INIT);

    ESP_UTILS_LOG_TRACE_EXIT_WITH_THIS();

    return true;
}

} // namespace esp_panel::drivers

#endif // ESP_PANEL_DRIVERS_LCD_ENABLE_SIMPLE
