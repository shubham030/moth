#include "panel.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_st7796.h"
#include "esp_log.h"

#define PIN_MOSI 20
#define PIN_CLK  21
#define PIN_CS   23
#define PIN_DC   26
#define PIN_RST  27
#define PIN_BL   28

/* Landscape mirror combo — the one this panel verified against; if the
 * picture is flipped on your unit, adjust these two. */
#define MIRROR_X false
#define MIRROR_Y true

static const char *TAG = "panel";
static esp_lcd_panel_handle_t s_panel;
static uint16_t *s_fb565; /* PSRAM scratch, panel byte order */

esp_err_t panel_init(void)
{
    gpio_config_t bl = { .pin_bit_mask = 1ULL << PIN_BL, .mode = GPIO_MODE_OUTPUT };
    ESP_ERROR_CHECK(gpio_config(&bl));
    gpio_set_level(PIN_BL, 1);

    const spi_bus_config_t buscfg = {
        .sclk_io_num = PIN_CLK,
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = GPIO_NUM_NC,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = PANEL_LANDSCAPE_W * PANEL_LANDSCAPE_H * 2,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    const esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = PIN_DC,
        .cs_gpio_num = PIN_CS,
        .pclk_hz = 80 * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 3,
        .trans_queue_depth = 10,
    };
    esp_lcd_panel_io_handle_t io;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST,
                                             &io_config, &io));
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_RST,
        .color_space = ESP_LCD_COLOR_SPACE_BGR,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7796(io, &panel_config, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(s_panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_panel, MIRROR_X, MIRROR_Y));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

    s_fb565 = heap_caps_malloc(PANEL_LANDSCAPE_W * PANEL_LANDSCAPE_H * 2,
                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_fb565) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "ST7796 up, landscape %dx%d", PANEL_LANDSCAPE_W, PANEL_LANDSCAPE_H);
    return ESP_OK;
}

esp_err_t panel_present_argb(const uint32_t *argb)
{
    const int n = PANEL_LANDSCAPE_W * PANEL_LANDSCAPE_H;
    for (int i = 0; i < n; i++) {
        uint32_t p = argb[i];
        uint16_t px = (uint16_t)((((p >> 16) & 0xF8) << 8) |
                                 (((p >> 8) & 0xFC) << 3) |
                                 ((p & 0xFF) >> 3));
        s_fb565[i] = (uint16_t)((px >> 8) | (px << 8)); /* panel byte order */
    }
    return esp_lcd_panel_draw_bitmap(s_panel, 0, 0,
                                     PANEL_LANDSCAPE_W, PANEL_LANDSCAPE_H, s_fb565);
}
