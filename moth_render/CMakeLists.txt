cmake_minimum_required(VERSION 3.16)
project(moth_render CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # clangd

add_library(moth_render STATIC
  src/scene.cpp
  src/layout.cpp
  src/paint.cpp
)
target_include_directories(moth_render PUBLIC include)
target_compile_options(moth_render PRIVATE -Wall -Wextra)

# TODO(R2): ThorVG via pkg-config (brew install thorvg) or vendored subdir.
# find_package(PkgConfig); pkg_check_modules(THORVG thorvg)

# Desktop harness — built only when SDL2 is available.
find_package(SDL2 QUIET)
if(SDL2_FOUND)
  add_executable(harness harness/main.c)
  target_link_libraries(harness PRIVATE moth_render SDL2::SDL2)
  message(STATUS "SDL2 found — building harness")
else()
  message(STATUS "SDL2 not found — core library only (brew install sdl2 for the harness)")
endif()
