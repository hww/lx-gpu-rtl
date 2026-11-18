CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic -I. -I../sim -I../tools -I../third_party/stb
LDFLAGS  ?=
LDLIBS   ?=

VPATH    := ../sim:../tools

EMU_SRCS := memory.cpp ch_addr_gen.cpp ch_blender.cpp ch_cache.cpp core_alu.cpp gpu_top.cpp host_if.cpp
TOP_SRCS := main_top.cpp image_io.cpp 
OBJS     := $(notdir $(EMU_SRCS:.cpp=.o) $(TOP_SRCS:.cpp=.o))
BIN      := sim

.PHONY: all clean debug sim-alu sim-ch-wr sim-loop sim-outer-loop sim-sprite

all: $(BIN)

$(BIN): $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

debug:
	$(CXX) -std=c++17 -O0 -g3 -Wall -Wextra -Wpedantic -I. -I../sim -I../tools -I../third_party/stb \
	    -fsanitize=address,undefined -fno-omit-frame-pointer \
	    -o $(BIN)-debug $(EMU_SRCS) ../sim/image_io.cpp ../tools/dis.cpp ../tools/asm.cpp main_top.cpp $(LDFLAGS) $(LDLIBS)

sim-alu: $(BIN)
	./$(BIN) -v -trace -prog ../sim/test_alu.asm -gpuaddr 32

sim-ch-wr: $(BIN)
	./$(BIN) -v -trace -prog ../sim/test_channel_write.asm -gpuaddr 32

sim-loop: $(BIN)
	./$(BIN) -v -trace -prog ../sim/test_loop.asm -gpuaddr 32

sim-outer-loop: $(BIN)
	./$(BIN) -v -trace -prog ../sim/test_outer_loop.asm -gpuaddr 32

sim-sprite: $(BIN)
	./$(BIN) \
	    -trace \
	    -prog ../sim/test_sprite.asm -gpuaddr 32 \
	    -s1in ../images/sprite01_16x16_8bpp.png -s1fmt 8bpp -s1w 16 -s1h 16 -s1off 0x0400 \
	    -din ../images/bg01_320x200_8bpp.png -dfmt 8bpp -dw 320 -dh 200 -doff 0x0800 \
	    -out ../images/output_emu.png

clean:
	rm -f $(OBJS) $(BIN) $(BIN)-debug *.d