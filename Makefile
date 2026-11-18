YOSYS    := yosys
NEXTPNR  := nextpnr-ecp5
ECPPACK  := ecppack
DEVICE   := 25k
PACKAGE  := CABGA256
LPF      := icesugar_pro.lpf
BUILD    := build

RTL := rtl/fmt_pkg.sv rtl/alu.sv rtl/regfile.sv rtl/cond_eval.sv rtl/fetch_unit.sv rtl/microcode_rom.sv \
       rtl/gpu_cache.sv rtl/gpu_host.sv rtl/sequencer.sv rtl/microcode_rom.sv \
       rtl/top.sv rtl/top_fpga.sv
TOP := top_fpga

.PHONY: all synth pnr bit clean top stat sprite \
        sim-top sim-alu sim-pixel-engine \
        sim-pixel-blender sim-gpu-cache sim-channel asm dis

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

# ================================================================
# sim-top: Vtop, без tb_top_wrapper
# ================================================================
top: asm $(wildcard tools/*.h) | $(BUILD)
	mkdir -p $(BUILD)/sim_top
	verilator --cc --exe --build -j 1 \
		-DSIMULATION \
		-CFLAGS "-I$(CURDIR)/tools -I$(CURDIR)/third_party/stb -I$(CURDIR)/sim" \
		-Wno-fatal -Wno-TIMESCALEMOD -Wno-MULTITOP -Wno-UNUSEDSIGNAL -Wno-EOFNEWLINE -Wno-UNOPTFLAT \
		--trace \
		--public-flat-rw \
		--top-module top \
		-Mdir $(CURDIR)/$(BUILD)/sim_top \
		rtl/fmt_pkg.sv rtl/alu.sv rtl/regfile.sv rtl/cond_eval.sv rtl/fetch_unit.sv rtl/microcode_rom.sv \
		rtl/pixel_engine.sv rtl/pixel_blender.sv rtl/gpu_cache.sv rtl/channel_unit.sv \
		rtl/top.sv \
		$(CURDIR)/sim/main_top.cpp $(CURDIR)/sim/image_io.cpp

sim-top: top
	./$(BUILD)/sim_top/Vtop -trace -prog sim/test_alu.asm -gpuaddr 32

sim-top-v: top
	./$(BUILD)/sim_top/Vtop -v -trace -prog sim/test_sprite.asm -gpuaddr 32

sim-top-alu-v: top
	./$(BUILD)/sim_top/Vtop -v -trace -prog sim/test_alu.asm -gpuaddr 32

sim-top-sprite: top
	./$(BUILD)/sim_top/Vtop \
	    -trace \
		-prog sim/test_sprite.asm -gpuaddr 32 \
		-din images/bg01_320x200_8bpp.png -dfmt 8bpp -dw 320 -dh 200 -doff 0x0400 \
		-out images/output.png

sim-top-sprite-v: top
	./$(BUILD)/sim_top/Vtop \
	    -v -trace \
		-prog sim/test_sprite.asm -gpuaddr 32 -maxcycles 30000 \
		-din images/bg01_320x200_8bpp.png -dfmt 8bpp -dw 320 -dh 200 -doff 0x0400 \
		-out images/output.png	

# ---- остальные sim-* (модульные тесты) ----
sim-alu: | $(BUILD)
	mkdir -p $(BUILD)/sim_alu
	verilator --cc --exe --build -j 1 -DSIMULATION \
	    -CFLAGS "-I$(CURDIR)/tools" \
	    -Wno-fatal -Wno-TIMESCALEMOD -Wno-MULTITOP -Wno-UNUSEDSIGNAL -Wno-EOFNEWLINE \
	    --top-module alu -Mdir $(CURDIR)/$(BUILD)/sim_alu \
	    rtl/alu.sv $(CURDIR)/sim/tb_alu.cpp
	./$(BUILD)/sim_alu/Valu

sim-pixel-engine: | $(BUILD)
	mkdir -p $(BUILD)/sim_pixel_engine
	verilator --cc --exe --build -j 1 -DSIMULATION \
	    -CFLAGS "-I$(CURDIR)/tools" \
	    -Wno-fatal -Wno-TIMESCALEMOD -Wno-MULTITOP -Wno-UNUSEDSIGNAL -Wno-EOFNEWLINE \
	    --top-module pixel_engine -Mdir $(CURDIR)/$(BUILD)/sim_pixel_engine \
	    rtl/fmt_pkg.sv rtl/pixel_engine.sv $(CURDIR)/sim/tb_pixel_engine.cpp
	./$(BUILD)/sim_pixel_engine/Vpixel_engine

sim-pixel-blender: | $(BUILD)
	mkdir -p $(BUILD)/sim_pixel_blender
	verilator --cc --exe --build -j 1 -DSIMULATION \
	    -CFLAGS "-I$(CURDIR)/tools" \
	    -Wno-fatal -Wno-TIMESCALEMOD -Wno-MULTITOP -Wno-UNUSEDSIGNAL -Wno-EOFNEWLINE \
	    --top-module pixel_blender -Mdir $(CURDIR)/$(BUILD)/sim_pixel_blender \
	    rtl/fmt_pkg.sv rtl/pixel_blender.sv $(CURDIR)/sim/tb_pixel_blender.cpp
	./$(BUILD)/sim_pixel_blender/Vpixel_blender

sim-cache: | $(BUILD)
	mkdir -p $(BUILD)/sim_gpu_cache
	verilator --cc --exe --build -j 1 -DSIMULATION \
	    -CFLAGS "-I$(CURDIR)/tools" \
	    -Wno-fatal -Wno-TIMESCALEMOD -Wno-MULTITOP -Wno-UNUSEDSIGNAL -Wno-EOFNEWLINE \
	    --top-module gpu_cache -Mdir $(CURDIR)/$(BUILD)/sim_gpu_cache \
	    rtl/gpu_cache.sv $(CURDIR)/sim/tb_gpu_cache.cpp
	./$(BUILD)/sim_gpu_cache/Vgpu_cache


sim-fetch: | $(BUILD)
	mkdir -p $(BUILD)/tb_fetch_unit
	verilator --cc --exe --build -j 1 -DSIMULATION \
	    -CFLAGS "-I$(CURDIR)/tools" \
	    -Wno-fatal -Wno-TIMESCALEMOD -Wno-MULTITOP -Wno-UNUSEDSIGNAL -Wno-EOFNEWLINE \
	    --top-module fetch_unit -Mdir $(CURDIR)/$(BUILD)/tb_fetch_unit \
	    rtl/fetch_unit.sv  rtl/cond_eval.sv $(CURDIR)/sim/tb_fetch_unit.cpp
	./$(BUILD)/tb_fetch_unit/Vfetch_unit

sim-cond: | $(BUILD)
	mkdir -p $(BUILD)/cond_eval
	verilator --cc --exe --build -j 1 -DSIMULATION \
	    -CFLAGS "-I$(CURDIR)/tools" \
	    -Wno-fatal -Wno-TIMESCALEMOD -Wno-MULTITOP -Wno-UNUSEDSIGNAL -Wno-EOFNEWLINE \
	    --top-module cond_eval -Mdir $(CURDIR)/$(BUILD)/cond_eval \
	    rtl/cond_eval.sv $(CURDIR)/sim/tb_cond_eval.cpp
	./$(BUILD)/cond_eval/Vcond_eval


sim-channel: | $(BUILD)
	mkdir -p $(BUILD)/tb_channel_unit
	verilator --cc --exe --build -j 1 -DSIMULATION \
	    -CFLAGS "-I$(CURDIR)/tools" \
	    -Wno-fatal -Wno-TIMESCALEMOD -Wno-MULTITOP -Wno-UNUSEDSIGNAL -Wno-EOFNEWLINE \
	    --trace \
	    --top-module channel_unit -Mdir $(CURDIR)/$(BUILD)/tb_channel_unit \
	    rtl/fmt_pkg.sv rtl/channel_unit.sv rtl/pixel_engine.sv rtl/pixel_blender.sv rtl/gpu_cache.sv \
	    $(CURDIR)/sim/tb_channel_unit.cpp
	./$(BUILD)/tb_channel_unit/Vchannel_unit


# ================================================================
# synth / pnr / bit (без asm/pack)
# ================================================================
# Плоская статистика (для оценки всей FPGA)
synth: | $(BUILD)
	$(YOSYS) -q -l $(BUILD)/synth.log -p \
	    "read_verilog -sv $(RTL); hierarchy -top $(TOP); synth_ecp5 -json $(BUILD)/$(TOP).json -no-rw-check; tee -q -o $(BUILD)/stat_flat.txt stat"
	@echo "=== flat stat ==="
	@cat $(BUILD)/stat_flat.txt

# Иерархическая статистика (для понимания, где что тратится)
stat: | $(BUILD)
	$(YOSYS) -q -l $(BUILD)/stat_hier.log -p \
	    "read_verilog -sv $(RTL); hierarchy -top $(TOP); synth_ecp5 -noflatten -no-rw-check; tee -q -o $(BUILD)/stat_hier.txt stat -top $(TOP)"
	@echo "=== hierarchical stat ==="
	@cat $(BUILD)/stat_hier.txt

pnr: synth
	$(NEXTPNR) --$(DEVICE) --speed 6 --package $(PACKAGE) \
	    --json $(BUILD)/$(TOP).json --lpf $(LPF) --textcfg $(BUILD)/$(TOP).config

bit: pnr
	$(ECPPACK) --input $(BUILD)/$(TOP).config --bit $(BUILD)/$(TOP).bit --svf $(BUILD)/$(TOP).svf

clean:
	rm -rf $(BUILD)

