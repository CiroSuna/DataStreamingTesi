#!/bin/bash
mkdir -p build
if [ "$1" = "debug" ]; then
    cmake -S . -B build -DDEBUG_BUILD=ON
else
    cmake -S . -B build -DDEBUG_BUILD=OFF
fi
