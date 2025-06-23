cmake_minimum_required(VERSION 3.20)
project(vibe C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -Wall")

include_directories(src/)

add_executable(vibe src/main.c src/mach.c src/rv.c src/hw/clint.c src/hw/plic.c src/hw/uart.c src/hw/rtc.c src/hw/virtio_net.c src/hw/virtio_blk.c src/hw/virtio_rng.c)

target_link_libraries(vibe curses)