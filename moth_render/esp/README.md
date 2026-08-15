# moth_render on ESP32-P4

R4-lite bring-up: the same demo scene as the SDL harness on the board's 3.5"
ST7796 panel. The core (`../src`) compiles unchanged — this directory is only
the platform shim (SPI panel + BOOT button + FreeRTOS loop).

- Panel: ST7796 over SPI2 @ 80MHz, landscape 480×320. If the image is
  mirrored on your unit, adjust `MIRROR_X/MIRROR_Y` in `panel.c`.
- No touch on this board: the BOOT button (GPIO35) rotates the cards'
  `flex_grow` factors and re-fires the pulse animation.
- Present path: `mr_commit()` returns true → ARGB8888 → byte-swapped RGB565
  scratch in PSRAM → full-frame `esp_lcd_panel_draw_bitmap` (~30ms).
  Idle frames flush nothing. Real damage tracking is R3.

## Build & flash

```
. ~/esp/esp-idf/export.sh
idf.py build
idf.py dfu          # or `idf.py flash` — DFU is the fallback if your
                    # board's UART is unavailable
```

Then put the board in DFU mode and `dfu-util -d 303a:0011 -D build/dfu.bin`
(or `idf.py dfu-flash`). Serial monitor still works for logs if USB-Serial-JTAG
is up: `idf.py monitor`.
