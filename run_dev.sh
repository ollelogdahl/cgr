#!/bin/sh

#cmake --preset dev
#cmake --build --preset dev
make dev -j$(nproc)
GLSLC_PATH=~/bin/glslc .target/dev/cgr
