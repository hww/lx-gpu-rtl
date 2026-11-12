YOSYS    := yosys
NEXTPNR  := nextpnr-ecp5
ECPPACK  := ecppack
DEVICE   := 25k
PACKAGE  := CABGA256
LPF      := icesugar_pro.lpf

RTL := rtl/regfile.sv rtl/alu.sv rtl/pixel_packer.sv rtl/addr_packer.sv \
       rtl/pixel_buffer.sv rtl/sequencer.sv rtl/microcode_rom.sv rtl/top.sv \
       rtl/top_fpga.sv

.PHONY: all synth pnr bit clean

all: bit

synth:
	$(YOSYS) -p "read_verilog -sv $(RTL); hierarchy -top top_fpga; synth_ecp5 -json top_fpga.json -no-rw-check; stat"

pnr: synth
	$(NEXTPNR) --$(DEVICE) --speed 6 --package $(PACKAGE) \
	    --json top_fpga.json --lpf $(LPF) --textcfg top_fpga.config

bit: pnr
	$(ECPPACK) --input top_fpga.config --bit top_fpga.bit --svf top_fpga.svf

clean:
	rm -f top_fpga.json top_fpga.config top_fpga.bit top_fpga.svf