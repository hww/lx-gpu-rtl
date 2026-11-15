mkdir -p build/sim_top
verilator --cc --exe --build -j 1 \
    -DSIMULATION \
    -CFLAGS "-I/mnt/d/projects/hww/_fpga/lx-gpu-rtl/third_party/stb -I/mnt/d/projects/hww/_fpga/lx-gpu-rtl/sim" \
    -Wno-fatal -Wno-TIMESCALEMOD -Wno-MULTITOP -Wno-UNUSEDSIGNAL -Wno-EOFNEWLINE \
    --trace \
    --top-module tb_top_wrapper \
    -Mdir /mnt/d/projects/hww/_fpga/lx-gpu-rtl/build/sim_top \
    rtl/alu.sv rtl/regfile.sv rtl/sequencer.sv rtl/microcode_rom.sv \
    rtl/pixel_engine.sv rtl/pixel_blender.sv rtl/gpu_cache.sv rtl/top.sv \
    sim/tb_top_wrapper.sv /mnt/d/projects/hww/_fpga/lx-gpu-rtl/sim/main_top.cpp /mnt/d/projects/hww/_fpga/lx-gpu-rtl/sim/image_io.cpp
make[1]: Entering directory '/mnt/d/projects/hww/_fpga/lx-gpu-rtl/build/sim_top'
g++  -I.  -MMD -I/usr/share/verilator/include -I/usr/share/verilator/include/vltstd -DVM_COVERAGE=0 -DVM_SC=0 -DVM_TRACE=1 -DVM_TRACE_FST=0 -DVM_TRACE_VCD=1 -faligned-new -fcf-protection=none -Wno-bool-operation -Wno-overloaded-virtual -Wno-shadow -Wno-sign-compare -Wno-uninitialized -Wno-unused-but-set-parameter -Wno-unused-but-set-variable -Wno-unused-parameter -Wno-unused-variable     -I/mnt/d/projects/hww/_fpga/lx-gpu-rtl/third_party/stb -I/mnt/d/projects/hww/_fpga/lx-gpu-rtl/sim   -Os -c -o main_top.o /mnt/d/projects/hww/_fpga/lx-gpu-rtl/sim/main_top.cpp
