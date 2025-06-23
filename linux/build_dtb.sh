#!/bin/bash
# Build device tree blob from source
# Requires dtc (device tree compiler)

if ! command -v dtc &> /dev/null; then
    echo "dtc not found. Please install device tree compiler."
    echo "On macOS: brew install dtc"
    echo "On Ubuntu/Debian: apt-get install device-tree-compiler"
    exit 1
fi

dtc -I dts -O dtb -o vibe.dtb vibe.dts
echo "Device tree blob built: vibe.dtb"