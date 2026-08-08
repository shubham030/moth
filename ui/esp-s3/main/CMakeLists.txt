# The VM, the renderer and the bindings are all platform-free; only panel.c
# and app_main.c are specific to this board.
set(ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")

idf_component_register(
  SRCS
    app_main.c
    "${ROOT}/moth_render/esp-s3/main/panel.c"
    "${ROOT}/vm/src/vm.c"
    "${ROOT}/vm/src/object.c"
    "${ROOT}/vm/src/verify.c"
    "${ROOT}/vm/src/loader.c"
    "${ROOT}/ui/src/moth_ui.c"
    "${ROOT}/moth_render/src/scene.cpp"
    "${ROOT}/moth_render/src/layout.cpp"
    "${ROOT}/moth_render/src/paint.cpp"
    "${ROOT}/moth_render/src/text.cpp"
  INCLUDE_DIRS
    .
    "${ROOT}/vm/include"
    "${ROOT}/ui/include"
    "${ROOT}/moth_render/include"
    "${ROOT}/moth_render/src"
    "${ROOT}/moth_render/esp-s3/main"
  PRIV_REQUIRES esp_lcd esp_driver_gpio esp_driver_spi esp_driver_i2c esp_timer
  EMBED_FILES program.mothb
)
