# Builds everything that runs on the host: the VM, the renderer, the UI
# bindings between them, and the two desktop runners.
#
#   cmake -B build . && cmake --build build
#   ./build/vm/mothrun program.mothb        # no display
#   ./build/mothsim program.mothb           # with a window
cmake_minimum_required(VERSION 3.16)
project(moth C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(vm)
add_subdirectory(moth_render)

add_library(moth_ui STATIC ui/src/moth_ui.c)
target_include_directories(moth_ui PUBLIC ui/include)
target_link_libraries(moth_ui PUBLIC moth_vm moth_render)
target_compile_options(moth_ui PRIVATE -Wall -Wextra)

find_package(SDL2 QUIET)
if(SDL2_FOUND)
  add_executable(mothsim vm/host/mothsim.c)
  target_link_libraries(mothsim PRIVATE moth_ui SDL2::SDL2)
  target_compile_options(mothsim PRIVATE -Wall -Wextra)
  # moth_render is C++, so the link needs the C++ runtime.
  set_target_properties(mothsim PROPERTIES LINKER_LANGUAGE CXX)
else()
  message(STATUS "SDL2 not found — skipping mothsim (brew install sdl2)")
endif()
