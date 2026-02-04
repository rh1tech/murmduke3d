#!/bin/bash
# build.sh - Simple development build
# USB HID is DISABLED by default (USB Serial Console enabled for debugging)
# Use -DUSB_HID_ENABLED=ON to enable USB keyboard/mouse support

rm -rf ./build
mkdir build
cd build
cmake -DPICO_PLATFORM=rp2350 -DBOARD_VARIANT=M2 -DUSB_HID_ENABLED=OFF ..
make -j4
