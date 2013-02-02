#!/bin/bash
gcc -shared -fPIC -Wall -O2 -o oslayr.so oslayr.c `pkg-config --cflags lua5.1`

