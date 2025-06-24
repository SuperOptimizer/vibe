cmake_minimum_required(VERSION 3.20)
project(vibe C)

set(CMAKE_C_STANDARD 23)

include_directories(src/)

# Source files
set(VIBE_SOURCES 
    src/main.c 
    src/mach.c 
    src/rv.c 
    src/hw/clint.c 
    src/hw/plic.c 
    src/hw/uart.c 
    src/hw/rtc.c 
    src/hw/virtio.c 
    src/hw/virtio_blk.c 
    src/hw/virtio_net.c 
    src/hw/virtio_rng.c
)

# Main executable
add_executable(vibe ${VIBE_SOURCES})
target_link_libraries(vibe curses)

# Release configuration - maximum optimization
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(vibe PUBLIC 
        -O3 
        -march=native 
        -mtune=native 
        -flto 
        -fomit-frame-pointer 
        -finline-functions 
        -funroll-loops 
        -ffast-math 
        -Wall 
        -Wextra
    )
    target_link_options(vibe PUBLIC 
        -fuse-ld=lld 
        -flto 
        -s  # Strip symbols
    )

# Debug configuration
elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(vibe PUBLIC 
        -Og 
        -g3 
        -fno-omit-frame-pointer 
        -Wall 
        -Wextra 
        -Wpedantic
    )
    target_link_options(vibe PUBLIC 
        -fuse-ld=lld 
        -g3
    )

# RelWithDebInfo configuration
elseif(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    target_compile_options(vibe PUBLIC 
        -O2 
        -g 
        -Wall 
        -Wextra
    )
    target_link_options(vibe PUBLIC 
        -fuse-ld=lld 
        -g
    )

# MinSizeRel configuration
elseif(CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
    target_compile_options(vibe PUBLIC 
        -Os 
        -Wall
    )
    target_link_options(vibe PUBLIC 
        -fuse-ld=lld 
        -s
    )

# Default to Debug if no build type specified
else()
    set(CMAKE_BUILD_TYPE Debug)
    target_compile_options(vibe PUBLIC 
        -Og 
        -g3 
        -fno-omit-frame-pointer 
        -Wall 
        -Wextra 
        -Wpedantic
    )
    target_link_options(vibe PUBLIC 
        -fuse-ld=lld 
        -g3
    )
endif()

add_executable(vibe-san ${VIBE_SOURCES})
target_link_libraries(vibe-san curses)
target_compile_options(vibe-san PUBLIC
    -Og
    -g3
    -fno-omit-frame-pointer
    -Wall
    -Wextra
    -Wpedantic
    -fsanitize=address,undefined
    -fno-sanitize-recover=all
)
target_link_options(vibe-san PUBLIC
    -fuse-ld=lld
    -g3
    -fsanitize=address,undefined
)