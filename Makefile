YOSYS    := yosys
NEXTPNR  := nextpnr-ecp5
ECPPACK  := ecppack
DEVICE   := 25k
PACKAGE  := CABGA256
LPF      := icesugar_pro.lpf
BUILD    := build

RTL := rtl/regfile.sv rtl/alu.sv rtl/pixel_packer.sv rtl/addr_packer.sv \
       rtl/pixel_buffer.sv rtl/sequencer.sv rtl/microcode_rom.sv rtl/top.sv

# top_fpga ещё нет; когда появится — добавь в RTL и замени top -> top_fpga
TOP  := top

.PHONY: all synth pnr bit clean sim-cpp-top stat-hier yosys

all: bit

$(BUILD):
	mkdir -p $(BUILD)

# ---- simulation ----
sim-cpp-top:
	mkdir -p $(BUILD)/sim_top
	verilator --cc --exe --build -j 1 \
	    -DSIMULATION \
	    -CFLAGS "-I$(CURDIR)/third_party/stb" \
	    -Wno-fatal -Wno-TIMESCALEMOD -Wno-MULTITOP -Wno-UNUSEDSIGNAL -Wno-EOFNEWLINE \
	    --trace \
	    --top-module tb_top_wrapper \
	    -Mdir $(CURDIR)/$(BUILD)/sim_top \
	    rtl/*.sv sim/tb_top_wrapper.sv sim/memory_arbiter_stub.sv \
	    $(CURDIR)/sim/main_top.cpp $(CURDIR)/$(IMAGE_IO_SRC)
	./$(BUILD)/sim_top/Vtb_top_wrapper

# ---- synthesis for PnR ----
synth: | $(BUILD)
	$(YOSYS) -q -l $(BUILD)/synth.log -p "read_verilog -sv $(RTL); hierarchy -top $(TOP); synth_ecp5 -abc9 -json $(BUILD)/$(TOP).json -no-rw-check; tee -q -o $(BUILD)/stat_flat.txt stat"
	@echo "=== flat stat ==="
	@sed -n '/Number of cells/,/^$$/p' $(BUILD)/stat_flat.txt

pnr: synth
	$(NEXTPNR) --$(DEVICE) --speed 6 --package $(PACKAGE) \
	    --json $(BUILD)/$(TOP).json --lpf $(LPF) --textcfg $(BUILD)/$(TOP).config

bit: pnr
	$(ECPPACK) --input $(BUILD)/$(TOP).config --bit $(BUILD)/$(TOP).bit --svf $(BUILD)/$(TOP).svf

# ---- hierarchical stat (per-module) ----
stat-hier: | $(BUILD)
	$(YOSYS) -q -l $(BUILD)/stat_hier.log -p "read_verilog -sv $(RTL); hierarchy -top $(TOP); synth_ecp5 -noflatten -no-rw-check; tee -q -o $(BUILD)/stat_hier.txt stat -top $(TOP)"
	@echo "Full report: $(BUILD)/stat_hier.txt"
	@awk '/^=== /{m=$$2} \
	      /LUT4/{l[m]=$$2} /TRELLIS_FF/{f[m]=$$2} /DP16KD/{d[m]=$$2} /MULT18X18D/{x[m]=$$2} \
	      /^=== /{seen[m]=1} \
	      END{printf "%-45s %7s %7s %5s %5s\n","module","LUT4","FF","EBR","DSP"; \
	          for(k in seen) printf "%-45s %7d %7d %5d %5d\n",k,l[k],f[k],d[k],x[k]}' $(BUILD)/stat_hier.txt

# ---- quick flat stat (no JSON) ----
yosys: | $(BUILD)
	$(YOSYS) -p "read_verilog -sv $(RTL); hierarchy -top $(TOP); synth_ecp5 -json $(BUILD)/$(TOP).json -no-rw-check; stat"

clean:
	rm -rf $(BUILD)


# --- image I/O (stb + обёртка) ---
IMAGE_IO_SRC := sim/image_io.cpp
IMAGE_IO_HDR := sim/image_io.h third_party/stb/stb_image.h third_party/stb/stb_image_write.h
STB_FLAGS    := -Ithird_party/stb -DSIMULATION

# --- roundtrip test: PNG -> pack -> mem -> unpack -> PNG (без GPU) ---
sim-image-roundtrip:
	mkdir -p $(BUILD)/sim_image_roundtrip
	g++ -std=c++17 -O2 -Wall $(STB_FLAGS) \
	    -o $(BUILD)/sim_image_roundtrip/image_roundtrip \
	    sim/main_image_roundtrip.cpp $(IMAGE_IO_SRC)
	./$(BUILD)/sim_image_roundtrip/image_roundtrip



.PHONY: sim-image-roundtrip



sim-alu-cpp:
	mkdir -p $(BUILD)/sim_alu
	verilator --cc --exe --build -j 1 \
	    -DSIMULATION \
	    -Wno-fatal -Wno-TIMESCALEMOD -Wno-MULTITOP -Wno-UNUSEDSIGNAL -Wno-EOFNEWLINE \
	    --top-module alu \
	    -Mdir $(CURDIR)/$(BUILD)/sim_alu \
	    rtl/alu.sv $(CURDIR)/sim/tb_alu.cpp
	./$(BUILD)/sim_alu/Valu