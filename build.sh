#!/bin/bash

set -xe

mkdir -p ./build
pushd ./build

clang -g ../code/xcb_game.c -o x11 -lX11 -lxcb -lX11-xcb -lxcb-icccm

popd
