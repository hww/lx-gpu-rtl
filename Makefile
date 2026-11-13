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
	    -Wno-fatal -Wno-TIMESCALEMOD -Wno-MULTITOP -Wno-UNUSEDSIGNAL -Wno-EOFNEWLINE \
	    --trace \
	    --top-module tb_top_wrapper \
	    -Mdir $(CURDIR)/$(BUILD)/sim_top \
	    rtl/*.sv sim/tb_top_wrapper.sv sim/memory_arbiter_stub.sv $(CURDIR)/sim/main_top.cpp
	./$(BUILD)/sim_top/Vtb_top_wrapper

# ---- synthesis for PnR ----
synth: | $(BUILD)
	$(YOSYS) -q -l $(BUILD)/synth.log -p "read_verilog -sv $(RTL); hierarchy -top $(TOP); synth_ecp5 -json $(BUILD)/$(TOP).json -no-rw-check; tee -q -o $(BUILD)/stat_flat.txt stat"
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
	@echo "Report: $(BUILD)/stat_hier.txt"
	@sed -n '/design hierarchy/,$$p' $(BUILD)/stat_hier.txt

# ---- quick flat stat (no JSON) ----
yosys: | $(BUILD)
	$(YOSYS) -p "read_verilog -sv $(RTL); hierarchy -top $(TOP); synth_ecp5 -json $(BUILD)/$(TOP).json -no-rw-check; stat"

clean:
	rm -rf $(BUILD)