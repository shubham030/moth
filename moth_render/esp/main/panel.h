/* ST7796 SPI panel for the carplay ESP32-P4 board, landscape addressing.
 * Wiring and init sequence lifted from the working mapcast bring-up.
 */
#pragma once

#include "esp_err.h"
#include <stdint.h>

#define PANEL_LANDSCAPE_W 480
#define PANEL_LANDSCAPE_H 320

esp_err_t panel_init(void);

/* Convert a PANEL_LANDSCAPE_W x PANEL_LANDSCAPE_H ARGB8888 buffer to the
 * panel's byte-swapped RGB565 and flush it over SPI (~30ms full frame). */
esp_err_t panel_present_argb(const uint32_t *argb);
