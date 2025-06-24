cmake_minimum_required(VERSION 3.20)
project(vibe C)

set(CMAKE_C_STANDARD 23)

include_directories(src/)

add_executable(vibe src/main.c src/mach.c src/rv.c src/hw/clint.c src/hw/plic.c src/hw/uart.c src/hw/rtc.c src/hw/virtio.c src/hw/virtio_blk.c src/hw/virtio_net.c src/hw/virtio_rng.c)

#-fsanitize=undefined,address
target_link_libraries(vibe curses)
target_link_options(vibe PUBLIC -fuse-ld=lld   -g3 )
target_compile_options(vibe PUBLIC  -Og -Wall  -g3 -fno-omit-frame-pointer )