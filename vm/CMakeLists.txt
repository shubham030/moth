cmake_minimum_required(VERSION 3.16)
project(moth_vm C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(moth_vm STATIC
  src/vm.c
  src/object.c
  src/verify.c
  src/loader.c
)
target_include_directories(moth_vm PUBLIC include)
target_compile_options(moth_vm PRIVATE -Wall -Wextra)

# Dart's % on doubles is fmod, and glibc keeps the math functions in a
# separate library. macOS folds them into libc, so a link that works there
# fails on Linux — PUBLIC so anything linking the VM inherits it.
find_library(MOTH_LIBM m)
if(MOTH_LIBM)
  target_link_libraries(moth_vm PUBLIC ${MOTH_LIBM})
endif()

add_executable(mothrun host/mothrun.c)
target_link_libraries(mothrun PRIVATE moth_vm)
target_compile_options(mothrun PRIVATE -Wall -Wextra)
