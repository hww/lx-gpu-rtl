g++ -std=c++17 -O2 -I tools -o build/asm tools/asm.cpp
mkdir -p build/sim_top
verilator --cc --exe --build -j 1 \
	-DSIMULATION \
	-CFLAGS "-I/mnt/d/projects/hww/_fpga/lx-gpu-rtl/tools -I/mnt/d/projects/hww/_fpga/lx-gpu-rtl/third_party/stb -I/mnt/d/projects/hww/_fpga/lx-gpu-rtl/sim" \
	-Wno-fatal -Wno-TIMESCALEMOD -Wno-MULTITOP -Wno-UNUSEDSIGNAL -Wno-EOFNEWLINE -Wno-UNOPTFLAT \
	--trace \
	--public-flat-rw \
	--top-module top \
	-Mdir /mnt/d/projects/hww/_fpga/lx-gpu-rtl/build/sim_top \
	rtl/fmt_pkg.sv rtl/alu.sv rtl/regfile.sv rtl/cond_eval.sv rtl/fetch_unit.sv rtl/microcode_rom.sv \
	rtl/pixel_engine.sv rtl/pixel_blender.sv rtl/gpu_cache.sv rtl/channel_unit.sv \
	rtl/top.sv \
	/mnt/d/projects/hww/_fpga/lx-gpu-rtl/sim/main_top.cpp /mnt/d/projects/hww/_fpga/lx-gpu-rtl/sim/image_io.cpp
make[1]: Entering directory '/mnt/d/projects/hww/_fpga/lx-gpu-rtl/build/sim_top'
g++  -I.  -MMD -I/usr/share/verilator/include -I/usr/share/verilator/include/vltstd -DVM_COVERAGE=0 -DVM_SC=0 -DVM_TRACE=1 -DVM_TRACE_FST=0 -DVM_TRACE_VCD=1 -faligned-new -fcf-protection=none -Wno-bool-operation -Wno-overloaded-virtual -Wno-shadow -Wno-sign-compare -Wno-uninitialized -Wno-unused-but-set-parameter -Wno-unused-but-set-variable -Wno-unused-parameter -Wno-unused-variable     -I/mnt/d/projects/hww/_fpga/lx-gpu-rtl/tools -I/mnt/d/projects/hww/_fpga/lx-gpu-rtl/third_party/stb -I/mnt/d/projects/hww/_fpga/lx-gpu-rtl/sim   -Os -c -o main_top.o /mnt/d/projects/hww/_fpga/lx-gpu-rtl/sim/main_top.cpp
g++ -Os  -I.  -MMD -I/usr/share/verilator/include -I/usr/share/verilator/include/vltstd -DVM_COVERAGE=0 -DVM_SC=0 -DVM_TRACE=1 -DVM_TRACE_FST=0 -DVM_TRACE_VCD=1 -faligned-new -fcf-protection=none -Wno-bool-operation -Wno-overloaded-virtual -Wno-shadow -Wno-sign-compare -Wno-uninitialized -Wno-unused-but-set-parameter -Wno-unused-but-set-variable -Wno-unused-parameter -Wno-unused-variable     -I/mnt/d/projects/hww/_fpga/lx-gpu-rtl/tools -I/mnt/d/projects/hww/_fpga/lx-gpu-rtl/third_party/stb -I/mnt/d/projects/hww/_fpga/lx-gpu-rtl/sim   -c -o verilated.o /usr/share/verilator/include/verilated.cpp
g++ -Os  -I.  -MMD -I/usr/share/verilator/include -I/usr/share/verilator/include/vltstd -DVM_COVERAGE=0 -DVM_SC=0 -DVM_TRACE=1 -DVM_TRACE_FST=0 -DVM_TRACE_VCD=1 -faligned-new -fcf-protection=none -Wno-bool-operation -Wno-overloaded-virtual -Wno-shadow -Wno-sign-compare -Wno-uninitialized -Wno-unused-but-set-parameter -Wno-unused-but-set-variable -Wno-unused-parameter -Wno-unused-variable     -I/mnt/d/projects/hww/_fpga/lx-gpu-rtl/tools -I/mnt/d/projects/hww/_fpga/lx-gpu-rtl/third_party/stb -I/mnt/d/projects/hww/_fpga/lx-gpu-rtl/sim   -c -o verilated_dpi.o /usr/share/verilator/include/verilated_dpi.cpp
