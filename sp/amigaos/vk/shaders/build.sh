#!/bin/bash
# Compiles the shaders of the Vulkan build into C arrays (../gen/*.h), which
# are checked in: the game's own build needs no shader compiler. Run in WSL
# or anywhere with glslangValidator:  bash build.sh
set -eu
cd "$(dirname "$(readlink -f "$0")")"
mkdir -p ../gen
for s in gl.vert gl0.frag gl1.frag gl2.frag; do
    n=vk_$(echo $s | tr . _)
    glslangValidator -V -I. --vn $n -o ../gen/$n.h $s > /tmp/$n.log || { cat /tmp/$n.log; exit 1; }
done
ls -l ../gen
