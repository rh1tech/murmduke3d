#!/bin/bash
rm -rf ./build
mkdir build
cd build
cmake -DPICO_PLATFORM=rp2350 -DBOARD_VARIANT=M2 ..
make -j4
