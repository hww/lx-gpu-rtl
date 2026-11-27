YOSYS    := yosys
NEXTPNR  := nextpnr-ecp5
ECPPACK  := ecppack
DEVICE   := 25k
PACKAGE  := CABGA256
LPF      := icesugar_pro.lpf
BUILD    := build

RTL := rtl/gpu_fmt_pkg.sv \
       rtl/core_alu.sv rtl/core_regfile.sv rtl/core_cond.sv rtl/core_fetch.sv rtl/core_rom.sv \
       rtl/ch_addr_gen.sv rtl/ch_blender.sv rtl/ch_cache.sv rtl/ch_top.sv rtl/ch_pixel_alu.sv \
       rtl/host_if.sv rtl/gpu_top.sv rtl/gpu_fpga.sv
TOP := gpu_fpga

.PHONY: all synth pnr bit clean top stat \
        sim-top sim-top-alu sim-channel-wr sim-loop sim-outer-loop sim-sprite \
        sim-core-alu sim-addr-gen sim-blender sim-cache sim-channel \
        sim-fetch sim-cond sim-host asm dis ch-pixel-alu

all: bit

$(BUILD):
	mkdir -p $(BUILD)

# ================================================================
# CLI tools: asm / dis (без питона)
# ================================================================
asm: tools/asm.cpp tools/asm.h tools/isa.h | $(BUILD)
	g++ -std=c++17 -O2 -I tools -o $(BUILD)/asm tools/asm.cpp

dis: tools/dis.cpp tools/disasm.h tools/isa.h | $(BUILD)
	g++ -std=c++17 -O2 -I tools -o $(BUILD)/dis tools/dis.cpp

isa-selftest: tools/isa.h | $(BUILD)
	g++ -std=c++17 -O2 -I tools -o $(BUILD)/isa-selftest tools/isa_selftest.cpp
	build/isa-selftest 
	
# ================================================================
# top: Vgpu_top (полная симуляция ядра)
# ================================================================
gpu-top: asm $(wildcard tools/*.h) | $(BUILD)
	mkdir -p $(BUILD)/sim_top
	verilator --cc -Irtl --exe --build -j 1 \
		-DSIMULATION \
		-CFLAGS "-I$(CURDIR)/tools -I$(CURDIR)/third_party/stb -I$(CURDIR)/sim" \
		-Wno-fatal -Wno-TIMESCALEMOD -Wno-MULTITOP -Wno-UNUSEDSIGNAL -Wno-EOFNEWLINE -Wno-UNOPTFLAT \
		--trace \
		--public-flat-rw \
		--top-module gpu_top \
		-Mdir $(CURDIR)/$(BUILD)/sim_top \
		rtl/gpu_fmt_pkg.sv \
		rtl/core_alu.sv rtl/core_regfile.sv rtl/core_cond.sv rtl/core_fetch.sv rtl/core_rom.sv \
		rtl/ch_addr_gen.sv rtl/ch_blender.sv rtl/ch_cache.sv rtl/ch_top.sv \
		rtl/gpu_top.sv \
		$(CURDIR)/sim/main_gpu_top.cpp $(CURDIR)/sim/image_io.cpp

sim-alu: gpu-top
	./$(BUILD)/sim_top/Vgpu_top -v -trace -prog sim/test_alu.asm -gpuaddr 32

sim-ch-wr: gpu-top
	./$(BUILD)/sim_top/Vgpu_top -v -trace -prog sim/test_channel_write.asm -gpuaddr 32

sim-loop: gpu-top
	./$(BUILD)/sim_top/Vgpu_top -v -trace -prog sim/test_loop.asm -gpuaddr 32

sim-outer-loop: gpu-top
	./$(BUILD)/sim_top/Vgpu_top -v -trace -prog sim/test_outer_loop.asm -gpuaddr 32

sim-sprite: gpu-top
	./$(BUILD)/sim_top/Vgpu_top \
	 	-v \
	    -trace \
		-prog sim/test_sprite.asm -gpuaddr 32 \
		-s1in images/sprite01_16x16_8bpp.png -s1fmt 8bpp -s1w 16 -s1h 16 -s1off 0x0400 \
		-din images/bg01_320x200_8bpp.png -dfmt 8bpp -dw 320 -dh 200 -doff 0x0800 \
		-out images/output.png


# ================================================================
# Модульные тесты (sim-*)
# ================================================================

## === CODER =====================================================

core-alu: | $(BUILD)
	mkdir -p $(BUILD)/sim_core_alu
	verilator --cc -Irtl --exe --build -j 1 -DSIMULATION \
	    -CFLAGS "-I$(CURDIR)/tools" \
	    -Wno-fatal -Wno-TIMESCALEMOD -Wno-MULTITOP -Wno-UNUSEDSIGNAL -Wno-EOFNEWLINE \
	    --top-module core_alu -Mdir $(CURDIR)/$(BUILD)/sim_core_alu \
	    rtl/core_alu.sv $(CURDIR)/sim/tb_core_alu.cpp
	./$(BUILD)/sim_core_alu/Vcore_alu

core-fetch: | $(BUILD)
	mkdir -p $(BUILD)/sim_fetch
	verilator --cc -Irtl --exe --build -j 1 -DSIMULATION \
	    -CFLAGS "-I$(CURDIR)/tools" \
	    -Wno-fatal -Wno-TIMESCALEMOD -Wno-MULTITOP -Wno-UNUSEDSIGNAL -Wno-EOFNEWLINE \
	    --top-module core_fetch -Mdir $(CURDIR)/$(BUILD)/sim_fetch \
	    rtl/core_fetch.sv rtl/core_cond.sv $(CURDIR)/sim/tb_core_fetch.cpp
	./$(BUILD)/sim_fetch/Vcore_fetch

core-cond: | $(BUILD)
	mkdir -p $(BUILD)/sim_cond
	verilator --cc -Irtl --exe --build -j 1 -DSIMULATION \
	    -CFLAGS "-I$(CURDIR)/tools" \
	    -Wno-fatal -Wno-TIMESCALEMOD -Wno-MULTITOP -Wno-UNUSEDSIGNAL -Wno-EOFNEWLINE \
	    --top-module core_cond -Mdir $(CURDIR)/$(BUILD)/sim_cond \
	    rtl/core_cond.sv $(CURDIR)/sim/tb_core_cond.cpp
	./$(BUILD)/sim_cond/Vcore_cond

host-if: | $(BUILD)
	mkdir -p $(BUILD)/sim_host
	verilator --cc -Irtl --exe --build -j 1 -DSIMULATION \
	    -CFLAGS "-I$(CURDIR)/tools" \
	    -Wno-fatal -Wno-TIMESCALEMOD -Wno-MULTITOP -Wno-UNUSEDSIGNAL -Wno-EOFNEWLINE \
	    --top-module host_if -Mdir $(CURDIR)/$(BUILD)/sim_host \
	    rtl/host_if.sv $(CURDIR)/sim/tb_host_if.cpp
	./$(BUILD)/sim_host/Vhost_if

## == CHANNEL =====================================================

ch-cache: | $(BUILD)
	mkdir -p $(BUILD)/sim_cache
	verilator --cc -Irtl --exe --build -j 1 -DSIMULATION \
	    -CFLAGS "-I$(CURDIR)/tools" \
	    -Wno-fatal -Wno-TIMESCALEMOD -Wno-MULTITOP -Wno-UNUSEDSIGNAL -Wno-EOFNEWLINE \
	    --top-module ch_cache -Mdir $(CURDIR)/$(BUILD)/sim_cache \
	    rtl/ch_cache.sv $(CURDIR)/sim/tb_ch_cache.cpp
	./$(BUILD)/sim_cache/Vch_cache

ch-addr-gen: | $(BUILD)
	mkdir -p $(BUILD)/sim_addr_gen
	verilator --cc -Irtl --exe --build -j 1 -DSIMULATION \
	    -CFLAGS "-I$(CURDIR)/tools" \
	    -Wno-fatal -Wno-TIMESCALEMOD -Wno-MULTITOP -Wno-UNUSEDSIGNAL -Wno-EOFNEWLINE \
	    --top-module ch_addr_gen -Mdir $(CURDIR)/$(BUILD)/sim_addr_gen \
	    rtl/ch_pixel_alu.sv rtl/gpu_fmt_pkg.sv rtl/ch_addr_gen.sv $(CURDIR)/sim/tb_ch_addr_gen.cpp
	./$(BUILD)/sim_addr_gen/Vch_addr_gen

ch-blender: | $(BUILD)
	mkdir -p $(BUILD)/sim_blender
	verilator --cc -Irtl --exe --build -j 1 -DSIMULATION \
	    -CFLAGS "-I$(CURDIR)/tools" \
	    -Wno-fatal -Wno-TIMESCALEMOD -Wno-MULTITOP -Wno-UNUSEDSIGNAL -Wno-EOFNEWLINE \
	    --top-module ch_blender -Mdir $(CURDIR)/$(BUILD)/sim_blender \
	    rtl/ch_pixel_alu.sv rtl/gpu_fmt_pkg.sv rtl/ch_blender.sv $(CURDIR)/sim/tb_ch_blender.cpp
	./$(BUILD)/sim_blender/Vch_blender

ch-pixel-alu: | $(BUILD)
	mkdir -p $(BUILD)/sim_pix_alu
	verilator --cc -Irtl --exe --build -j 1 -DSIMULATION \
	    -CFLAGS "-I$(CURDIR)/tools" \
	    -Wno-fatal -Wno-TIMESCALEMOD -Wno-MULTITOP -Wno-UNUSEDSIGNAL -Wno-EOFNEWLINE \
	    --top-module ch_pixel_alu -Mdir $(CURDIR)/$(BUILD)/sim_pix_alu \
	    rtl/ch_pixel_alu.sv $(CURDIR)/sim/tb_ch_pixel_alu.cpp
	./$(BUILD)/sim_pix_alu/Vch_pixel_alu

ch-top: | $(BUILD)
	mkdir -p $(BUILD)/sim_channel
	verilator --cc -Irtl --exe --build -j 1 -DSIMULATION \
	    -CFLAGS "-I$(CURDIR)/tools" \
	    -Wno-fatal -Wno-TIMESCALEMOD -Wno-MULTITOP -Wno-UNUSEDSIGNAL -Wno-EOFNEWLINE \
	    --trace \
	    --top-module ch_top -Mdir $(CURDIR)/$(BUILD)/sim_channel \
	    rtl/ch_pixel_alu.sv rtl/gpu_fmt_pkg.sv rtl/ch_top.sv rtl/ch_addr_gen.sv rtl/ch_blender.sv rtl/ch_cache.sv \
	    $(CURDIR)/sim/tb_ch_top.cpp
	./$(BUILD)/sim_channel/Vch_top





# ================================================================
# synth / pnr / bit (без asm/pack)
# ================================================================
synth: | $(BUILD)
	$(YOSYS) -q -l $(BUILD)/synth.log -p \
	    "read_verilog -sv -Irtl $(RTL); hierarchy -top $(TOP); synth_ecp5 -json $(BUILD)/$(TOP).json -no-rw-check; tee -q -o $(BUILD)/stat_flat.txt stat"
	@echo "=== flat stat ==="
	@cat $(BUILD)/stat_flat.txt

stat: | $(BUILD)
	$(YOSYS) -q -l $(BUILD)/stat_hier.log -p \
	    "read_verilog -sv -Irtl $(RTL); hierarchy -top $(TOP); synth_ecp5 -noflatten -no-rw-check; tee -q -o $(BUILD)/stat_hier.txt stat -top $(TOP)"
	@echo "=== hierarchical stat ==="
	@cat $(BUILD)/stat_hier.txt

pnr: synth
	$(NEXTPNR) --$(DEVICE) --speed 6 --package $(PACKAGE) \
	    --json $(BUILD)/$(TOP).json --lpf $(LPF) --textcfg $(BUILD)/$(TOP).config

bit: pnr
	$(ECPPACK) --input $(BUILD)/$(TOP).config --bit $(BUILD)/$(TOP).bit --svf $(BUILD)/$(TOP).svf

clean:
	rm -rf $(BUILD)
