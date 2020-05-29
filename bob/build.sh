#!/usr/bin/env sh
cd "$(dirname "$0")"
clang -std=c++17 -I../lib -Wall -O0 -o bob bob.cpp || exit 1
# ./bob -r .. -o bob/out.ninja -c debug -v
