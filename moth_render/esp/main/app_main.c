/* moth_render on an ESP32-P4 dev board: the same demo scene as the SDL
 * harness, presented over SPI. No touch on this board — the BOOT button
 * (GPIO35) drives scene mutations instead of mr_pointer.
 */
#include "moth_render.h"
#include "panel.h"

#include <inttypes.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define PIN_BOOT_BTN 35

static const char *TAG = "moth";
static mr_node_id s_cards[3];
static mr_node_id s_pulse;

static void on_event(const mr_event *ev, void *user)
{
    (void)user;
    ESP_LOGI(TAG, "event: node=%" PRIu32 " kind=%d value=%.1f", ev->node, ev->kind, ev->value);
}

static void build_scene(void)
{
    mr_node_id root = mr_root();
    mr_set_u32(root, MR_PROP_BG_COLOR, 0xFF1A1B26);
    mr_set_f32(root, MR_PROP_PADDING, 16);
    mr_set_f32(root, MR_PROP_GAP, 12);

    mr_node_id title = mr_node_create(MR_NODE_LABEL);
    mr_set_str(title, MR_PROP_TEXT, "moth on ESP32-P4");
    mr_set_f32(title, MR_PROP_FONT_SIZE, 20);
    mr_set_u32(title, MR_PROP_TEXT_COLOR, 0xFFC0CAF5);
    mr_attach(root, title, -1);

    mr_node_id row = mr_node_create(MR_NODE_BOX);
    mr_set_u32(row, MR_PROP_FLEX_DIRECTION, MR_ROW);
    mr_set_f32(row, MR_PROP_GAP, 12);
    mr_set_f32(row, MR_PROP_FLEX_GROW, 1);
    mr_attach(root, row, -1);

    const uint32_t colors[] = {0xFF7AA2F7, 0xFFBB9AF7, 0xFF9ECE6A};
    for (int i = 0; i < 3; i++) {
        s_cards[i] = mr_node_create(MR_NODE_BOX);
        mr_set_u32(s_cards[i], MR_PROP_BG_COLOR, colors[i]);
        mr_set_f32(s_cards[i], MR_PROP_FLEX_GROW, (float)(i + 1));
        mr_attach(row, s_cards[i], -1);
    }

    s_pulse = mr_node_create(MR_NODE_BOX);
    mr_set_u32(s_pulse, MR_PROP_BG_COLOR, 0xFFF7768E);
    mr_set_f32(s_pulse, MR_PROP_HEIGHT, 24);
    mr_attach(root, s_pulse, -1);
    mr_anim_start(s_pulse, MR_PROP_OPACITY, 1.0f, 0.15f, 900, MR_EASE_IN_OUT);
}

/* BOOT press: rotate the cards' grow factors and re-run the pulse — a
 * batched scene mutation exercising layout + native animation on-device. */
static void on_boot_press(void)
{
    static int shift = 0;
    shift = (shift + 1) % 3;
    for (int i = 0; i < 3; i++) {
        mr_set_f32(s_cards[i], MR_PROP_FLEX_GROW, (float)(((i + shift) % 3) + 1));
    }
    mr_anim_start(s_pulse, MR_PROP_OPACITY, 1.0f, 0.15f, 900, MR_EASE_IN_OUT);
    ESP_LOGI(TAG, "grow shift %d", shift);
}

void app_main(void)
{
    ESP_ERROR_CHECK(panel_init());

    const gpio_config_t btn = {
        .pin_bit_mask = 1ULL << PIN_BOOT_BTN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&btn));

    mr_config cfg = {PANEL_LANDSCAPE_W, PANEL_LANDSCAPE_H};
    if (!mr_init(&cfg)) {
        ESP_LOGE(TAG, "mr_init failed");
        return;
    }
    mr_set_event_sink(on_event, NULL);
    build_scene();
    ESP_LOGI(TAG, "contract v%" PRIu32 ", scene up — BOOT button mutates", mr_contract_version());

    int64_t prev_us = esp_timer_get_time();
    int last_btn = 1;
    int frames = 0;
    int64_t fps_mark_us = prev_us;

    for (;;) {
        int lvl = gpio_get_level(PIN_BOOT_BTN);
        if (last_btn == 1 && lvl == 0) {
            on_boot_press();
        }
        last_btn = lvl;

        int64_t now_us = esp_timer_get_time();
        mr_tick((uint32_t)((now_us - prev_us) / 1000));
        prev_us = now_us;

        if (mr_commit()) {
            { int dx, dy, dw, dh; mr_damage(&dx, &dy, &dw, &dh); panel_present_argb(mr_framebuffer(), dy, dh); }
            frames++;
        }
        if (now_us - fps_mark_us >= 1000000) {
            if (frames > 0) {
                ESP_LOGI(TAG, "%d fps (painted frames)", frames);
            }
            frames = 0;
            fps_mark_us = now_us;
        }
        vTaskDelay(pdMS_TO_TICKS(16));
    }
}
