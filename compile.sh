#!/bin/bash
gcc -shared -fPIC -Wall -O0 -g -o oslayr.so oslayr.c `pkg-config --cflags lua5.1`

