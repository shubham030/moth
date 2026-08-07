/* Waveshare ESP32-S3-Touch-AMOLED-1.75C: CO5300 466x466 round AMOLED (QSPI)
 * + CST9217 touch (I2C).
 */
#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#define PANEL_W 466
#define PANEL_H 466

esp_err_t panel_init(void);

/* Convert ARGB8888 -> byte-swapped RGB565 and flush. The S3 GPSPI cannot DMA
 * from PSRAM, so this chunks through a small internal DMA bounce buffer. */
esp_err_t panel_present_argb(const uint32_t *argb);

/* Poll touch. Returns true and fills x/y while a finger is down. */
bool panel_touch_read(int *x, int *y);
