# The VM core (vm/src) is platform-free C11 — compiled straight in.
# program.mothb is the compiled Dart app, embedded in the firmware image.
# Regenerate it with:
#   dart run tools/mothc/bin/mothc.dart examples/board_demo.dart \
#     -o vm/esp/main/program.mothb
set(VM_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

idf_component_register(
  SRCS
    app_main.c
    hw_natives.c
    "${VM_ROOT}/src/vm.c"
    "${VM_ROOT}/src/object.c"
    "${VM_ROOT}/src/verify.c"
    "${VM_ROOT}/src/loader.c"
  INCLUDE_DIRS "${VM_ROOT}/include"
  PRIV_REQUIRES esp_driver_gpio esp_driver_ledc esp_driver_i2c esp_driver_uart esp_adc esp_timer nvs_flash
  EMBED_FILES program.mothb
)
