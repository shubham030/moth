cmake_minimum_required(VERSION 3.16)
project(moth_render CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # clangd

add_library(moth_render STATIC
  src/scene.cpp
  src/layout.cpp
  src/paint.cpp
  src/text.cpp
)
target_include_directories(moth_render PUBLIC include PRIVATE src)
target_compile_options(moth_render PRIVATE -Wall -Wextra)

# TODO(R2): ThorVG via pkg-config (brew install thorvg) or vendored subdir.
# find_package(PkgConfig); pkg_check_modules(THORVG thorvg)

# Paint-cost regression suite: budgets on pixels visited per frame. Compiled
# separately from the library so MR_PROFILE's counters exist here and cost
# nothing everywhere else. See docs/PERF_REVIEW.md.
add_executable(render_perf_test
  test/perf_test.cpp
  src/scene.cpp
  src/layout.cpp
  src/paint.cpp
  src/text.cpp
)
target_include_directories(render_perf_test PRIVATE include src)
target_compile_definitions(render_perf_test PRIVATE MR_PROFILE=1)
target_compile_options(render_perf_test PRIVATE -Wall -Wextra)

# Contract test for the interactive controls: gestures in, events and
# pixels out. Plain library build — no profiling needed.
add_executable(render_controls_test
  test/controls_test.cpp
  src/scene.cpp
  src/layout.cpp
  src/paint.cpp
  src/text.cpp
)
target_include_directories(render_controls_test PRIVATE include src)
target_compile_options(render_controls_test PRIVATE -Wall -Wextra)

# Desktop harness — built only when SDL2 is available.
find_package(SDL2 QUIET)
if(SDL2_FOUND)
  add_executable(harness harness/main.c)
  target_link_libraries(harness PRIVATE moth_render SDL2::SDL2)
  message(STATUS "SDL2 found — building harness")
else()
  message(STATUS "SDL2 not found — core library only (brew install sdl2 for the harness)")
endif()
