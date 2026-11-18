# lx-gpu-rtl

RTL implementation, functional C++ emulator, and toolchain for **LX-GPU** — a programmable 16-bit 2D graphics coprocessor for Z80 / MSX-class systems (Aleste LX).

Architecture specification: [XiAleste / Aleste LX GPU Documentation](https://hww.github.io/aleste-xi-doc/docs/gpu/docs/01_architecture.html).

## Architecture at a Glance

- **16-bit datapath, 32-bit fixed microinstructions** executed on a 3-stage pipeline (`F -> D -> X`) with 4096×32 microcode BRAM (`core_rom`).
- **Four instruction classes** (`[30:29]`):
  - **REG-REG** (`00`): `DST = DST op SRC` (6-bit operand map `0..63`)
  - **LDI16** (`01`): conditional 16-bit immediate load into `DST`, flags untouched
  - **CONTROL** (`10`): `NOP`, `JMP` (immediate or register-indirect), `CALL`, `RET`, `HALT`, `IRQ`, `FLUSH`
  - **REG-IMM11** (`11`): `DST = DST op imm11` with sign/zero extension
- **16-bit ALU** (`core_alu`): logic (`MOV`, `MVN`, `AND`, `ANDN`, `OR`, `ORN`, `XOR`, `XNOR`), arithmetic (`ADD`, `SUB`, `ADC`, `SBC`), shifts (`ROR`, `SHR`, `SAR`), and signed `16×16 -> 32` `MUL` (upper 16 bits latched to `MHI`).
- **Predication & Hardware Loops**:
  - 4-bit ARM-style condition codes (`AL`, `EQ`, `NE`, `CS`, `CC`, `MI`, `PL`, `VS`, `VC`, `HI`, `LS`, `GE`, `LT`, `GT`, `LE`).
  - `SHADOW` register saves `{V, C, N, Z, PC+1}` on `CALL`.
  - Zero-overhead hardware loops via `LOOP_CNT`, `LOOP_TGT`, and instruction bit `[31]` (`L`).
- **4 Unified Memory Channels (`CH0..CH3`)**:
  - Each channel has `DATA`, `IDX`, `IDXH`, `STEP`, and `MODEPAGE` registers, backed by a 1-word write-back cache (`ch_cache`) and 20-bit fractional pixel address generator (`ch_addr_gen`).
  - Supported formats: `16 bpp`, `8 bpp`, `4 bpp`, `2 bpp` (read-only), `1 bpp` (read-only), and byte pass-through (`LO8`, `HI8`).
  - Reading or writing `DATA[ch]` automatically advances `IDX[ch]` by `STEP[ch]`. Out-of-page access (`64 KiB` boundary) halts the core with error code `2`.
- **ROM Bootloader**:
  - Permanently resides in protected ROM words `0..31` (`boot/bootloader.asm`; writes to `0..31` halt with error code `1`).
  - On `GPU_START`, hardware preloads `CH0` (`16 bpp`, `STEP=1`) to the host packet address, copies `[SIZE, START, payload...]` into microcode ROM via `ROMA`/`ROML`/`ROMH`, and jumps to `START`.

## Microcode Example

Copying a 16-pixel span in `8 bpp` from `CH0` (source) to `CH1` (destination) using a hardware loop:

```asm
.org 0x0020

    ; Configure CH0 (source): 8 bpp (mode=1), page 0x00, byte offset 0x0200
    LDI  MODEPAGE0, 0x0100
    LDI  STEP0, 1
    LDI  IDX0, 0x0200

    ; Configure CH1 (dest): 8 bpp (mode=1), page 0x00, byte offset 0x0300
    LDI  MODEPAGE1, 0x0100
    LDI  STEP1, 1
    LDI  IDX1, 0x0300

    ; Hardware loop: 16 iterations
    LDI  LOOP_CNT, 16
    LDI  LOOP_TGT, copy_loop

copy_loop:
    MOV  R0, DATA0          ; Read pixel from CH0 (IDX0 += STEP0)
    MOV  DATA1, R0 !l       ; Write pixel to CH1 (IDX1 += STEP1), loop back

    FLUSH 1                 ; Write back dirty cache word on CH1
    HALT 0                  ; Stop cleanly
```

## Repository Layout

```text
├── rtl/            SystemVerilog RTL (gpu_top, core_fetch, core_alu, core_regfile,
│                   core_rom, ch_top, ch_addr_gen, ch_blender, ch_cache, host_if, gpu_fpga)
├── boot/           Microcode bootloader source (bootloader.asm)
├── tools/          Assembler (asm.h / asm.cpp), disassembler (disasm.h / dis.cpp), ISA defs (isa.h)
├── sim/            Verilator top-level testbench (main_top.cpp), PNG I/O (image_io.*), .asm tests
├── emul/           Cycle-free functional C++ emulator matching the RTL behavior
└── images/         Test sprites and backgrounds
```

## Building & Running Tests

### 1. Functional C++ Emulator (`emul/`)
Fast instruction-accurate C++ model sharing the same CLI flags, `.asm` loader, and PNG I/O as the Verilator testbench:

```bash
cd emul
make sim-alu          # Run ALU & loop test
make sim-ch-wr        # Run channel packing/write test
make sim-loop         # Run hardware loop test
make sim-outer-loop   # Run nested loop test
make sim-sprite       # Run 8bpp sprite blit test (outputs images/output_emu.png)
```

### 2. RTL Simulation (`sim/`)
Cycle-accurate simulation via Verilator (supports `-trace` for VCD waveform dumps):

```bash
make sim-alu
make sim-sprite
```

### 3. Standalone Assembler & Disassembler (`tools/`)
```bash
g++ -std=c++17 -O2 tools/asm.cpp -o asm
g++ -std=c++17 -O2 tools/dis.cpp -o disasm
