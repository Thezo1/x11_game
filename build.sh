#!/bin/bash

set -xe

mkdir -p ./build
pushd ./build

clang -g -Werror ../code/x11_game.c -o x11 -lX11 -lXext -lGL -ludev

popd
