# The VM, the renderer and the bindings are all platform-free; only panel.c
# and app_main.c are specific to this board.
set(ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")

idf_component_register(
  SRCS
    app_main.c
    hotpush.c
    pushstore.c
    serialpush.c
    "${ROOT}/moth_render/esp-s3/main/panel.c"
    "${ROOT}/vm/src/vm.c"
    "${ROOT}/vm/src/object.c"
    "${ROOT}/vm/src/verify.c"
    "${ROOT}/vm/src/loader.c"
    # The push receiver is plain BSD sockets, which lwIP provides — the same
    # file mothsim uses on the desktop, unchanged.
    "${ROOT}/vm/host/push.c"
    "${ROOT}/vm/host/hmac_sha256.c"
    "${ROOT}/ui/src/moth_ui.c"
    "${ROOT}/moth_render/src/scene.cpp"
    "${ROOT}/moth_render/src/layout.cpp"
    "${ROOT}/moth_render/src/paint.cpp"
    "${ROOT}/moth_render/src/text.cpp"
  INCLUDE_DIRS
    .
    "${ROOT}/vm/include"
    "${ROOT}/vm/host"
    "${ROOT}/ui/include"
    "${ROOT}/moth_render/include"
    "${ROOT}/moth_render/src"
    "${ROOT}/moth_render/esp-s3/main"
  PRIV_REQUIRES esp_lcd esp_driver_gpio esp_driver_spi esp_driver_i2c esp_timer
                esp_wifi esp_netif esp_event nvs_flash lwip esp_partition
                esp_driver_usb_serial_jtag
  EMBED_FILES program.mothb
)

# Frame profiling, off in a normal build. `make fps` (tools/fpsbench) passes
# -DMOTH_FPSBENCH=1: MOTH_FRAME_PROFILE turns on the phase logging in
# app_main.c, MR_PROFILE the per-primitive counters in moth_render, and
# app_main provides the clock both need.
if(MOTH_FPSBENCH)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE
    MOTH_FRAME_PROFILE=1 MR_PROFILE=1)
endif()
