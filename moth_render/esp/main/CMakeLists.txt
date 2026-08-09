# The moth_render core (src/*.cpp) is platform-free C++17 — compiled directly
# into this component rather than vendored or duplicated.
set(MR_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

idf_component_register(
  SRCS
    app_main.c
    panel.c
    "${MR_ROOT}/src/scene.cpp"
    "${MR_ROOT}/src/layout.cpp"
    "${MR_ROOT}/src/paint.cpp"
    "${MR_ROOT}/src/text.cpp"
  INCLUDE_DIRS . "${MR_ROOT}/include" "${MR_ROOT}/src"
  PRIV_REQUIRES esp_lcd esp_driver_gpio esp_driver_spi esp_timer
)
