#include "panel.h"

#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch_cst9217.h"
#include "esp_log.h"

/* Waveshare 1.75C pin map (from the MOMO bring-up — the C variant differs
 * from the non-C board: LCD_RST 1, TOUCH_RST 2). */
#define PIN_I2C_SDA   15
#define PIN_I2C_SCL   14
#define PIN_LCD_CS    12
#define PIN_LCD_PCLK  38
#define PIN_LCD_D0    4
#define PIN_LCD_D1    5
#define PIN_LCD_D2    6
#define PIN_LCD_D3    7
#define PIN_LCD_RST   1
#define PIN_TOUCH_RST 2
#define PIN_TOUCH_INT 11

/* Internal-DMA bounce chunk. Even line count keeps the CO5300's even/odd
 * window alignment rule satisfied for every chunk. */
#define CHUNK_LINES 30

static const char *TAG = "panel";
static i2c_master_bus_handle_t s_i2c_bus;
static esp_lcd_panel_io_handle_t s_panel_io;
static esp_lcd_panel_handle_t s_panel;
static esp_lcd_touch_handle_t s_touch;
static uint16_t *s_chunk; /* internal DMA-capable bounce buffer */

/* Vendor init sequence (Waveshare BSP via MOMO). */
static const co5300_lcd_init_cmd_t s_lcd_init_cmds[] = {
    {0xFE, (uint8_t[]){0x20}, 1, 0},
    {0x19, (uint8_t[]){0x10}, 1, 0},
    {0x1C, (uint8_t[]){0xA0}, 1, 0},
    {0xFE, (uint8_t[]){0x00}, 1, 0},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0},
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 0},
    {0x51, (uint8_t[]){0xFF}, 1, 0},
    {0x63, (uint8_t[]){0xFF}, 1, 0},
    {0x2A, (uint8_t[]){0x00, 0x06, 0x01, 0xD7}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xD1}, 4, 600},
    {0x11, NULL, 0, 600},
    {0x29, NULL, 0, 0},
};

esp_err_t panel_init(void)
{
    const i2c_master_bus_config_t i2c_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .i2c_port = 0,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_cfg, &s_i2c_bus));

    const spi_bus_config_t buscfg = CO5300_PANEL_BUS_QSPI_CONFIG(
        PIN_LCD_PCLK, PIN_LCD_D0, PIN_LCD_D1, PIN_LCD_D2, PIN_LCD_D3,
        PANEL_W * CHUNK_LINES * 2);
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_config = CO5300_PANEL_IO_QSPI_CONFIG(PIN_LCD_CS, NULL, NULL);
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST,
                                             &io_config, &s_panel_io));

    co5300_vendor_config_t vendor_config = {
        .init_cmds = s_lcd_init_cmds,
        .init_cmds_size = sizeof(s_lcd_init_cmds) / sizeof(s_lcd_init_cmds[0]),
        .flags = { .use_qspi_interface = 1 },
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor_config,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_co5300(s_panel_io, &panel_config, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(s_panel, 0x06, 0));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_CST9217_CONFIG();
    tp_io_config.scl_speed_hz = 400000;
    esp_lcd_panel_io_handle_t tp_io = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(s_i2c_bus, &tp_io_config, &tp_io));

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = PANEL_W,
        .y_max = PANEL_H,
        .rst_gpio_num = PIN_TOUCH_RST,
        .int_gpio_num = PIN_TOUCH_INT,
        .levels = { .reset = 0, .interrupt = 0 },
    };
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_cst9217(tp_io, &tp_cfg, &s_touch));

    s_chunk = heap_caps_malloc(PANEL_W * CHUNK_LINES * 2,
                               MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!s_chunk) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "CO5300 %dx%d + CST9217 up", PANEL_W, PANEL_H);
    return ESP_OK;
}

esp_err_t panel_present_argb(const uint32_t *argb)
{
    for (int y = 0; y < PANEL_H; y += CHUNK_LINES) {
        int lines = PANEL_H - y < CHUNK_LINES ? PANEL_H - y : CHUNK_LINES;
        const uint32_t *src = argb + (size_t)y * PANEL_W;
        int n = PANEL_W * lines;
        for (int i = 0; i < n; i++) {
            uint32_t p = src[i];
            uint16_t px = (uint16_t)((((p >> 16) & 0xF8) << 8) |
                                     (((p >> 8) & 0xFC) << 3) |
                                     ((p & 0xFF) >> 3));
            s_chunk[i] = (uint16_t)((px >> 8) | (px << 8));
        }
        esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel, 0, y, PANEL_W, y + lines, s_chunk);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

bool panel_touch_read(int *x, int *y)
{
    esp_lcd_touch_read_data(s_touch);
    uint16_t tx, ty;
    uint8_t cnt = 0;
    if (esp_lcd_touch_get_coordinates(s_touch, &tx, &ty, NULL, &cnt, 1) && cnt > 0) {
        *x = tx;
        *y = ty;
        return true;
    }
    return false;
}
