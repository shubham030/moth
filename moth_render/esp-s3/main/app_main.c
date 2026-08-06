/* moth_render on the Waveshare ESP32-S3-Touch-AMOLED-1.75C: the demo scene
 * with real touch — taps rotate the cards' grow factors and re-fire the
 * pulse animation. Round display, so the scene keeps generous padding.
 */
#include <inttypes.h>

#include "moth_render.h"
#include "panel.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "moth";
static mr_node_id s_cards[3];
static mr_node_id s_pulse;
static bool s_clicked; /* set by sink, applied from the loop (contract §6:
                          the sink must not re-enter the API synchronously) */

static void on_event(const mr_event *ev, void *user)
{
    (void)user;
    ESP_LOGI(TAG, "event: node=%" PRIu32 " kind=%d at (%.0f,%.0f)",
             ev->node, ev->kind, ev->x, ev->y);
    if (ev->kind == MR_EV_CLICKED) {
        s_clicked = true;
    }
}

static void build_scene(void)
{
    mr_node_id root = mr_root();
    mr_set_u32(root, MR_PROP_BG_COLOR, 0xFF1A1B26);
    mr_set_f32(root, MR_PROP_PADDING, 72); /* keep content inside the circle */
    mr_set_f32(root, MR_PROP_GAP, 14);
    mr_set_u32(root, MR_PROP_MAIN_ALIGN, MR_ALIGN_CENTER);

    mr_node_id title = mr_node_create(MR_NODE_LABEL);
    mr_set_str(title, MR_PROP_TEXT, "moth on ESP32-S3");
    mr_set_f32(title, MR_PROP_FONT_SIZE, 20);
    mr_set_u32(title, MR_PROP_TEXT_COLOR, 0xFFC0CAF5);
    mr_attach(root, title, -1);

    mr_node_id row = mr_node_create(MR_NODE_BOX);
    mr_set_u32(row, MR_PROP_FLEX_DIRECTION, MR_ROW);
    mr_set_f32(row, MR_PROP_GAP, 14);
    mr_set_f32(row, MR_PROP_HEIGHT, 180);
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
    mr_set_f32(s_pulse, MR_PROP_HEIGHT, 20);
    mr_attach(root, s_pulse, -1);
    mr_anim_start(s_pulse, MR_PROP_OPACITY, 1.0f, 0.15f, 900, MR_EASE_IN_OUT);
}

static void on_tap(void)
{
    static int shift = 0;
    shift = (shift + 1) % 3;
    for (int i = 0; i < 3; i++) {
        mr_set_f32(s_cards[i], MR_PROP_FLEX_GROW, (float)(((i + shift) % 3) + 1));
    }
    mr_anim_start(s_pulse, MR_PROP_OPACITY, 1.0f, 0.15f, 900, MR_EASE_IN_OUT);
}

void app_main(void)
{
    ESP_ERROR_CHECK(panel_init());

    mr_config cfg = {PANEL_W, PANEL_H};
    if (!mr_init(&cfg)) {
        ESP_LOGE(TAG, "mr_init failed");
        return;
    }
    mr_set_event_sink(on_event, NULL);
    build_scene();
    ESP_LOGI(TAG, "contract v%" PRIu32 ", scene up — tap to mutate", mr_contract_version());

    int64_t prev_us = esp_timer_get_time();
    int frames = 0;
    int64_t fps_mark_us = prev_us;

    for (;;) {
        int tx, ty;
        bool down = panel_touch_read(&tx, &ty);
        static int last_x, last_y;
        if (down) {
            last_x = tx;
            last_y = ty;
        }
        mr_pointer(down ? tx : last_x, down ? ty : last_y, down);
        if (s_clicked) {
            s_clicked = false;
            on_tap();
        }

        int64_t now_us = esp_timer_get_time();
        mr_tick((uint32_t)((now_us - prev_us) / 1000));
        prev_us = now_us;

        if (mr_commit()) {
            panel_present_argb(mr_framebuffer());
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
