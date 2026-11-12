TOP ?= tb_regfile
SIM ?= pixel_buffer

.PHONY: lint sim sim-cpp clean

lint:
	verilator --lint-only -Wall -Wno-fatal -Wno-EOFNEWLINE -Wno-UNUSEDPARAM -Wno-TIMESCALEMOD -Wno-MULTITOP -Wno-UNUSEDSIGNAL rtl/*.sv

sim:
	./scripts/sim.sh $(TOP)

sim-cpp:
	mkdir -p build/sim_$(SIM)
	verilator --cc --exe --build -j 1 \
	    -Wno-fatal -Wno-TIMESCALEMOD -Wno-MULTITOP -Wno-UNUSEDSIGNAL -Wno-EOFNEWLINE \
	    --top-module tb_pixel_buffer_wrapper \
	    -Mdir $(CURDIR)/build/sim_$(SIM) \
	    rtl/pixel_buffer.sv sim/tb_pixel_buffer_wrapper.sv $(CURDIR)/sim/main_$(SIM).cpp
	./build/sim_$(SIM)/Vtb_pixel_buffer_wrapper

sim-cpp-top:
	mkdir -p build/sim_top
	verilator --cc --exe --build -j 1 \
	    -Wno-fatal -Wno-TIMESCALEMOD -Wno-MULTITOP -Wno-UNUSEDSIGNAL -Wno-EOFNEWLINE \
	    --trace \
	    --top-module tb_top_wrapper \
	    -Mdir $(CURDIR)/build/sim_top \
	    rtl/*.sv sim/tb_top_wrapper.sv sim/memory_arbiter_stub.sv $(CURDIR)/sim/main_top.cpp
	./build/sim_top/Vtb_top_wrapper

clean:
	rm -rf build