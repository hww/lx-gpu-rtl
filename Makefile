YOSYS    := yosys
NEXTPNR  := nextpnr-ecp5
ECPPACK  := ecppack
DEVICE   := 25k
PACKAGE  := CABGA256
LPF      := icesugar_pro.lpf
BUILD    := build

# RTL for synthesis (top_fpga is the FPGA top; top.sv is included since top_fpga instantiates it)
RTL := rtl/regfile.sv rtl/alu.sv rtl/pixel_blender.sv rtl/pixel_engine.sv \
       rtl/gpu_cache.sv rtl/sequencer.sv rtl/microcode_rom.sv \
       rtl/top.sv rtl/top_fpga.sv

TOP := top_fpga

# ---- Assembler ----
ASM       := python3 tools/asm.py
ASM_FLAGS := --raw --pad 4096

ASM_SRCS := $(wildcard rtl/*.asm) $(wildcard sim/*.asm)
ASM_HEXS := $(ASM_SRCS:.asm=.hex)

.PHONY: all synth pnr bit clean stat-hier yosys asm pack  \
        sim-top sim-sequencer sim-alu sim-pixel-engine sim-pixel-blender \
        sim-gpu-cache sim-image-roundtrip

all: bit

$(BUILD):
	mkdir -p $(BUILD)

# ---- Assembly: all .asm -> .hex (padded to ROM size) ----
asm: $(ASM_HEXS)

%.hex: %.asm tools/asm.py
	$(ASM) $(ASM_FLAGS) $< > $@

# ---- simulation (Verilator) ----
sim-top: asm pack
	mkdir -p $(BUILD)/sim_top
	verilator --cc --exe --build -j 1 \
	    -DSIMULATION \
	    -CFLAGS "-I$(CURDIR)/third_party/stb -I$(CURDIR)/sim" \
	    -Wno-fatal -Wno-TIMESCALEMOD -Wno-MULTITOP -Wno-UNUSEDSIGNAL -Wno-EOFNEWLINE \
	    --trace \
	    --top-module tb_top_wrapper \
	    -Mdir $(CURDIR)/$(BUILD)/sim_top \
	    rtl/alu.sv rtl/regfile.sv rtl/sequencer.sv rtl/microcode_rom.sv \
	    rtl/pixel_engine.sv rtl/pixel_blender.sv rtl/gpu_cache.sv rtl/top.sv \
	    sim/tb_top_wrapper.sv $(CURDIR)/sim/main_top.cpp $(CURDIR)/sim/image_io.cpp
	./$(BUILD)/sim_top/Vtb_top_wrapper -s1 sim/test_sprite.pkg -gpuaddr 0

sim-sequencer: asm pack
	mkdir -p $(BUILD)/sim_sequencer
	verilator --cc --exe --build -j 1 \
	    -DSIMULATION \
	    -Wno-fatal -Wno-TIMESCALEMOD -Wno-MULTITOP -Wno-UNUSEDSIGNAL -Wno-EOFNEWLINE \
	    --top-module sequencer \
	    -Mdir $(CURDIR)/$(BUILD)/sim_sequencer \
	    rtl/sequencer.sv $(CURDIR)/sim/tb_sequencer.cpp
	./$(BUILD)/sim_sequencer/Vsequencer

sim-alu:
	mkdir -p $(BUILD)/sim_alu
	verilator --cc --exe --build -j 1 \
	    -DSIMULATION \
	    -Wno-fatal -Wno-TIMESCALEMOD -Wno-MULTITOP -Wno-UNUSEDSIGNAL -Wno-EOFNEWLINE \
	    --top-module alu \
	    -Mdir $(CURDIR)/$(BUILD)/sim_alu \
	    rtl/alu.sv $(CURDIR)/sim/tb_alu.cpp
	./$(BUILD)/sim_alu/Valu

sim-pixel-engine:
	mkdir -p $(BUILD)/sim_pixel_engine
	verilator --cc --exe --build -j 1 \
	    -DSIMULATION \
	    -Wno-fatal -Wno-TIMESCALEMOD -Wno-MULTITOP -Wno-UNUSEDSIGNAL -Wno-EOFNEWLINE \
	    --top-module pixel_engine \
	    -Mdir $(CURDIR)/$(BUILD)/sim_pixel_engine \
	    rtl/pixel_engine.sv $(CURDIR)/sim/tb_pixel_engine.cpp
	./$(BUILD)/sim_pixel_engine/Vpixel_engine

sim-pixel-blender:
	mkdir -p $(BUILD)/sim_pixel_blender
	verilator --cc --exe --build -j 1 \
	    -DSIMULATION \
	    -Wno-fatal -Wno-TIMESCALEMOD -Wno-MULTITOP -Wno-UNUSEDSIGNAL -Wno-EOFNEWLINE \
	    --top-module pixel_blender \
	    -Mdir $(CURDIR)/$(BUILD)/sim_pixel_blender \
	    rtl/pixel_blender.sv $(CURDIR)/sim/tb_pixel_blender.cpp
	./$(BUILD)/sim_pixel_blender/Vpixel_blender

sim-gpu-cache:
	mkdir -p $(BUILD)/sim_gpu_cache
	verilator --cc --exe --build -j 1 \
	    -DSIMULATION \
	    -Wno-fatal -Wno-TIMESCALEMOD -Wno-MULTITOP -Wno-UNUSEDSIGNAL -Wno-EOFNEWLINE \
	    --top-module gpu_cache \
	    -Mdir $(CURDIR)/$(BUILD)/sim_gpu_cache \
	    rtl/gpu_cache.sv $(CURDIR)/sim/tb_gpu_cache.cpp
	./$(BUILD)/sim_gpu_cache/Vgpu_cache

# ---- image roundtrip (host-only, no Verilator) ----
IMAGE_IO_SRC := sim/image_io.cpp
STB_FLAGS    := -Ithird_party/stb -DSIMULATION

sim-image-roundtrip:
	mkdir -p $(BUILD)/sim_image_roundtrip
	g++ -std=c++17 -O2 -Wall $(STB_FLAGS) \
	    -o $(BUILD)/sim_image_roundtrip/image_roundtrip \
	    sim/main_image_roundtrip.cpp $(IMAGE_IO_SRC)
	./$(BUILD)/sim_image_roundtrip/image_roundtrip

# ---- synthesis for PnR ----
synth: asm pack | $(BUILD)
	$(YOSYS) -q -l $(BUILD)/synth.log -p \
	    "read_verilog -sv $(RTL); hierarchy -top $(TOP); synth_ecp5 -abc9 -json $(BUILD)/$(TOP).json -no-rw-check; tee -q -o $(BUILD)/stat_flat.txt stat"
	@echo "=== flat stat ==="
	@sed -n '/Number of cells/,/^$$/p' $(BUILD)/stat_flat.txt

pnr: synth
	$(NEXTPNR) --$(DEVICE) --speed 6 --package $(PACKAGE) \
	    --json $(BUILD)/$(TOP).json --lpf $(LPF) --textcfg $(BUILD)/$(TOP).config

bit: pnr
	$(ECPPACK) --input $(BUILD)/$(TOP).config --bit $(BUILD)/$(TOP).bit --svf $(BUILD)/$(TOP).svf

# ---- hierarchical stat (per-module) ----
stat-hier: asm pack | $(BUILD)
	$(YOSYS) -q -l $(BUILD)/stat_hier.log -p \
	    "read_verilog -sv $(RTL); hierarchy -top $(TOP); synth_ecp5 -noflatten -no-rw-check; tee -q -o $(BUILD)/stat_hier.txt stat -top $(TOP)"
	@echo "Full report: $(BUILD)/stat_hier.txt"
	@awk '/^=== /{m=$$2} \
	      /LUT4/{l[m]=$$2} /TRELLIS_FF/{f[m]=$$2} /DP16KD/{d[m]=$$2} /MULT18X18D/{x[m]=$$2} \
	      /^=== /{seen[m]=1} \
	      END{printf "%-45s %7s %7s %5s %5s\n","module","LUT4","FF","EBR","DSP"; \
	          for(k in seen) printf "%-45s %7d %7d %5d %5d\n",k,l[k],f[k],d[k],x[k]}' $(BUILD)/stat_hier.txt

# ---- quick flat stat (no JSON) ----
yosys: asm pack | $(BUILD)
	$(YOSYS) -p "read_verilog -sv $(RTL); hierarchy -top $(TOP); synth_ecp5 -json $(BUILD)/$(TOP).json -no-rw-check; stat"

clean:
	rm -rf $(BUILD)


# ---- Pack: .asm -> .pkg (WB package for RAM) ----
ASM_PKGS := $(patsubst %.asm,%.pkg,$(ASM_SRCS))

pack: $(ASM_PKGS)

%.pkg: %.asm tools/asm.py
	$(ASM) --pack --start 0x0020 $< > $@