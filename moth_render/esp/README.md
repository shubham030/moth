# ESP-IDF port (R4 — not started)

Plan: wrap the core library (`src/`, which has no platform dependencies) as an
ESP-IDF component; the platform layer replaces the SDL harness with:

- `esp_lcd` panel handle + DMA transfer of `mr_framebuffer()` damage regions
- PPA (ESP32-P4 2D accelerator) for blits/fills where profitable
- touch driver feeding `mr_pointer()`
- a FreeRTOS timer task driving `mr_tick()` / `mr_commit()`

Blocked on: R1 (layout goldens green) and R3 (damage tracking — full-frame
repaint over DMA at panel refresh is not viable on the P4).
