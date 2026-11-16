gdb -batch \
  -ex "run" \
  -ex "bt full" \
  -ex "frame 0" \
  -ex "info locals" \
  --args ./build/sim_top/Vtop \
    -prog sim/test_sprite.asm \
    -din images/bg01_320x200_8bpp.png -dfmt 8bpp -dw 320 -dh 200 -doff 0x0200 \
    -out images/output.png
