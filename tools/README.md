# LX-GPU Assembler and Disassembler

A set of three headers with no external dependencies (C++17):

| File | What's inside |
|---|---|
| `tools/isa.h` | the single source of truth about the ISA: code tables, word fields, operand map, `encode`/`decode`, and the **architecture rules** (`validate`) |
| `tools/asm.h` | assembler: text → 32-bit microcode words |
| `tools/disasm.h` | disassembler: word → text that the assembler reassembles into the same word |
| `tools/isa_selftest.cpp` | self-test of all of the above (`make isa-selftest`) |

The assembler rejects what `validate()` rejects, and the disassembler prints `.word` for everything `validate()` rejects. That way they cannot diverge.

---

## 1. Quick start

An assembly program (`fill.asm`):

```asm
        .org 0x20                   ; the program is loaded into ROM starting at address 0x20
start:  LDI   R1, #100              ; number of loop iterations
        MOV   LOOP_CNT, R1
        LDI   LOOP_TGT, #body       ; address of the start of the loop body
        MOV   R2, #0x55
body:   L MOV DATA0, R2             ; L = hardware loop: the body runs 100 times
        FLUSH 0                     ; push the last word of channel 0 out to memory
        HALT  0
```

From C++:

```cpp
#include "asm.h"
#include "disasm.h"

uint16_t org = 0;
std::vector<uint32_t> words = asm_assemble_file("fill.asm", &org);
if (words.empty()) return 1;            // error, messages have already been printed to stderr
for (size_t i = 0; i < words.size(); i++)
    printf("%04zX  %08X  %s\n", org + i, words[i], disasm32(words[i]));
```

Output:

```
0020  21020064  LDI R1, #0x0064
0021  01480040  MOV LOOP_CNT, R1
0022  214A0024  LDI LOOP_TGT, #0x0024
0023  61040055  MOV R2, #85
0024  81580080  L MOV DATA0, R2
0025  40006000  FLUSH 0
0026  40004000  HALT 0
```

From the command line (`tools/asm.cpp`): prints the words in hex, one per line; returns exit code 1 on error.

---

## 2. Line format

```
[label:] [L] MNEMONIC[.CONDITION][.L] operands [modifiers]   ; comment
```

- A comment starts with `;`.
- Registers, mnemonics, and conditions are **case-insensitive** (`mov r0, data0`). Labels are case-sensitive.
- A label is `[A-Za-z_][A-Za-z0-9_]*` followed by a colon. Multiple labels in a row are allowed (`a: b: NOP`), and a label may be on the same line as the instruction. A label cannot match a register name or an ALU mnemonic.
- The loop bit `L` can be written as a prefix (`L ADD R1, #1`) or as a suffix (`ADD.L R1, #1`, `ADD.NE.L R1, #1`). Both forms produce the same word.
- The condition is a suffix (`JMP.NE`, `MOV.EQ`); default is `AL` (always).
- One line is one instruction, and one instruction is one 32-bit word.

### Numbers

Decimal, `0x…` (hex), `0b…` (binary), with an optional sign `+`/`-`. A leading zero does **not** mean octal (`#010` is 10). In place of a number, immediates and `.word` may use a label, which means its address.

---

## 3. Operands

### 3.1 General-purpose registers

`R0 … R31`: 16-bit, readable and writable by any instruction.

### 3.2 Special operands

| Name | Read | Write | Note |
|---|---|---|---|
| `ZERO` | 0 | yes (ignored) | a "nowhere" destination |
| `FLAGS` | yes | yes | bits `[3:0] = V C N Z`; constant ≤ 15 |
| `BUSY` | yes | no | channel cache state |
| `SHADOW` | yes | yes | `[15:12]` flags at the time of `CALL`, `[11:0]` return address; a write writes all 16 bits |
| `LOOP_CNT` | yes | yes | hardware loop counter (16 bits) |
| `LOOP_TGT` | yes | yes | loop start address (12 bits, constant ≤ 4095) |
| `ROML`, `ROMH` | yes | yes | ROM access port (see 3.3); a read accesses the bus |
| `ROMA` | no | yes | ROM address (12 bits) |
| `MHI` | yes | no | high 16 bits of the result of the last `MUL` |
| `DATA0 … DATA3` | yes | yes | channel data: read/write a pixel or word, accesses the bus |
| `IDX0 … IDX3` | no | yes | channel pixel index |
| `IDXH0 … IDXH3` | no | yes | high bits of the index (3 bits, constant ≤ 7) |
| `STEP0 … STEP3` | no | yes | index auto-increment step |
| `PAGE0 … PAGE3` | no | yes | channel `{mode[10:8], page[7:0]}`; constant ≤ `0x7FF` |

Codes 42 and 43 are reserved: the assembler does not accept them. The operands `RSV42`/`RSV43` exist only in the name table.

### 3.3 ROM port

```
MOV ROMA, R1        ; address
MOV ROML, R2        ; latch the low 16 bits
MOV ROMH, R3        ; high 16 bits: the word is written to ROM[ROMA], ROMA++
MOV R4, ROML        ; read the low 16 bits (ROMA does not change)
MOV R5, ROMH        ; read the high 16 bits, ROMA++
```

Writing to ROM words 0…31 (the bootloader) stops the core with code `HALT 1`.

---

## 4. Instructions

All ALU instructions are two-operand: `d = d op s`. The destination `d` is also the first operand.

### 4.1 ALU

| Instruction | Code | Action |
|---|---|---|
| `MOV` | 0 | `d = s` |
| `MVN` | 1 | `d = ~s` |
| `AND` | 2 | `d &= s` |
| `BIC` | 3 | `d &= ~s` |
| `OR` | 4 | `d \|= s` |
| `ORN` | 5 | `d \|= ~s` |
| `XOR` | 6 | `d ^= s` |
| `XNOR` | 7 | `d = ~(d ^ s)` |
| `ADD` | 8 | `d += s` |
| `ADC` | 10 | `d += s + C` |
| `SUB` | 13 | `d -= s` |
| `SBC` | 15 | `d -= s + !C` |
| `ROR` | 16 | rotate right by `s[3:0]` |
| `SHR` | 18 | logical shift right by `s[3:0]` |
| `SAR` | 20 | arithmetic shift right by `s[3:0]` |
| `MUL` | 24 | signed 16×16: low 16 bits in `d`, high 16 in `MHI` |

Codes 9, 11, 12, 14 are the same `SUB`, `SBC`, `ADD`, `ADC` (the RTL ignores bit 2). The assembler always emits the codes from the table.

The source `s` is a register, a special operand, or a constant:

```asm
ADD  R1, R2           ; register
ADD  R1, #5           ; 11-bit constant
MOV  R3, DATA0        ; special operand
```

### 4.2 Compare and test (pseudo-instructions)

| Instruction | What it assembles to |
|---|---|
| `CMP a, b` | `SUB a, b` without writing the result, flags are updated |
| `TST a, b` | `AND a, b` without writing the result, flags are updated |

### 4.3 Load constant

`LDI Rd, #value`: 16-bit constant, range −32768…65535. It can also write to special operands (`LDI LOOP_TGT, #body`, `LDI ROMA, #0x20`). It does not change flags.

### 4.4 Control

| Instruction | Action |
|---|---|
| `JMP label` or `JMP address` | jump (address 0…4095) |
| `JMP Rn` | jump by register. `R0…R31` and special operands without a bus access work (`SHADOW`, `LOOP_*`, `MHI`, …), but not `DATA*` or `ROML/ROMH` |
| `CALL label` | call: `SHADOW = {flags, address of the next instruction}`. There is no `CALL` by register |
| `RET` | jump to `SHADOW[11:0]`, flags are not restored |
| `HALT [code]` | stop, code 0…127 (default 0; 0 = success) |
| `IRQ [code]` | interrupt the host, code 0…255 |
| `FLUSH channel` | write the channel's dirty word to memory and invalidate it; channel `0…3` or `CH0…CH3` |
| `NOP` | nothing |

A condition can be placed on any instruction: `JMP.NE loop`, `MOV.EQ R1, #0`, `HALT.CS 3`.

### 4.5 Conditions

| Suffix | Meaning | Suffix | Meaning |
|---|---|---|---|
| `AL` | always | `HI` | `C and not Z` |
| `EQ` | `Z` | `LS` | `not C or Z` |
| `NE` | `not Z` | `GE` | `N == V` |
| `CS` | `C` | `LT` | `N != V` |
| `CC` | `not C` | `GT` | `not Z and N == V` |
| `MI` | `N` | `LE` | `Z or N != V` |
| `PL` | `not N` | `VS` / `VC` | `V` / `not V` |

An instruction with a false condition changes nothing: neither registers, nor flags, nor ports.

---

## 5. Flags and modifiers

The flags `Z`, `N`, `C`, `V` are written only if the instruction has the `F` bit set and the condition is true.

- **By default `F = 1` for all ALU instructions except `MOV` and `MVN`.**
- `Z` and `N` come from the result. `C` and `V` are changed **only** by `ADD`, `ADC`, `SUB`, `SBC` (and `CMP`). Logic, shifts, and `MUL` leave `C` and `V` as they were: this is needed for multiword arithmetic (`ADD` then `ADC`).
- `C` is carry (for subtraction, "not borrow").

Modifiers at the end of the line:

| Modifier | Action |
|---|---|
| `!w` | do not write the result (flags still follow `F`) |
| `!f` | force `F = 1` |
| `!nf` | force `F = 0` |

```asm
SUB  R0, #1            ; F=1 by default: loop on Z
ADD  R1, #4 !nf        ; add without changing flags
MOV  R3, DATA0 !f      ; MOV is flagless by default; here Z/N follow the pixel value
MOV  ZERO, DATA0       ; read a pixel and advance the index without saving
```

**The assembler does not check flag hazards, `LOOP_*`, or `SHADOW`**: the core provides protection (a one-cycle delay if the next instruction reads what the previous one is still writing).

---

## 6. Hardware loop (the `L` bit)

The `L` bit on an instruction means "at the end of this word, if `LOOP_CNT > 1`, jump to `LOOP_TGT` and decrement the counter". With `LOOP_CNT = N` the body runs exactly `N` times; after the loop `LOOP_CNT = 1`, so it must be set again before the next loop.

```asm
        LDI  R1, #16
        MOV  LOOP_CNT, R1
        LDI  LOOP_TGT, #body
body:   ADD  R2, #1
        L MOV DATA0, R2         ; last word of the body; L is checked even if the condition is false
```

Rules:
- A taken branch in the same word takes priority over `L`.
- `L` is allowed on `JMP`, `RET`, `HALT`, and on ordinary instructions. `L JMP.EQ exit` exits the loop on a condition.
- **`L` on `CALL` is forbidden**: after `RET` the loop does not repeat but ends. If a call is needed inside the loop body, put `L` on the next word.
- Each iteration costs one extra cycle (a bubble after the branch). For hot loops, unroll the body over several entries and leave `L` on the last one.

---

## 7. Directives

| Directive | Action |
|---|---|
| `.org N` | move to address `N` (0…4095); the skipped space is filled with zero words; moving backwards is not allowed |
| `.word v1, v2, …` | raw 32-bit words (−2³¹…2³²−1), labels are allowed |

`asm_assemble_file` strips the leading `.org`: the returned words start at that address, and the address itself goes into `*out_org` (including `0`). If there is no leading `.org` directive, `*out_org` is left unchanged. `asm_assemble` returns the words together with the leading zeros from `.org`.

A program longer than 4096 words is not accepted.

---

## 8. What the assembler checks and rejects

All errors are printed at once in the format `file:line: error: message` with the source line; on any error the functions return an empty vector.

| Violation | Reason |
|---|---|
| a special operand as destination outside `MOV`/`LDI` (`ADD IDX0, #1`, `MVN IDX0, R1`) | the first ALU operand is read from the register file, where a special operand holds garbage |
| writing to `BUSY`, `MHI`, 42/43 | the operand is read-only or reserved |
| reading `ROMA`, `IDX*`, `IDXH*`, `STEP*`, `PAGE*` | the operand is write-only |
| two bus accesses in one instruction: `MOV DATA1, DATA0`, `MOV DATA0, ROML`, `MOV ROMH, ROML` | the core services one device per instruction; move the value into a register first |
| `CALL Rn` | there is no indirect call in the core |
| `JMP DATA0`, `JMP ROML` | a jump target cannot be a bus access |
| `L CALL …` | see section 6 |
| constant out of range: `#2048` (11 bits: −1024…2047), `LDI #65536`, `HALT 128`, `IRQ 256`, `FLUSH 4` | the field cannot hold the value; there is no silent truncation |
| constant for a special operand larger than the register size: `MOV IDXH0, #8`, `LDI ROMA, #4096`, `MOV FLAGS, #16` | the core would truncate the value |
| undefined or duplicate label, label named like a register | |
| wrong number of operands, unknown mnemonic/register/condition/modifier, empty operand, `.org` backwards | |

Not checked: whether a format value in `PAGE` is legal (`mode` is 3 bits, any value is accepted), the meaning of side effects (`IDX` increment), whether a branch lands in "data".

---

## 9. Disassembler

`const char* disasm32(uint32_t w)` returns a string in an internal buffer (the next call overwrites it).

The format is the same one the assembler accepts:

```
L SUB R0, #2          ; loop, F is the default, so there is no modifier
JMP.NE 0x003
JMP R1
MOV R3, DATA0 !f      ; F is not the default: a modifier is added
CMP R1, R2            ; SUB !w with F=1 is printed as CMP (likewise TST)
FLUSH 2
HALT 0
MOV R0, R0 !w         ; the zero word
.word 0x0000F123      ; a word the assembler cannot produce
```

A word is printed as `.word` if it fails `validate()` or contains bits that the text cannot express (reserved fields, alias codes 9/11/12/14, non-standard `WRITE`/`F` on `LDI` and control words, `SX` without bit 10, etc.). Therefore, for **any** 32-bit word, `asm(disasm32(w)) == w`.

---

## 10. Programming interface

```cpp
// asm.h
std::vector<uint32_t> asm_assemble_file(const char* path, uint16_t* out_org = nullptr);
std::vector<uint32_t> asm_assemble(const std::vector<std::string>& lines,
                                   const char* name = "<asm>");
bool asm_assemble_ex(const std::vector<std::string>& lines, const char* name,
                     AsmResult& res, FILE* err = stderr);   // err = nullptr: no messages
uint32_t asm_line(const std::string& raw, std::map<std::string,int>& labels,
                  int cur_pc, int* ok);                     // one line, for compatibility

// disasm.h
const char* disasm32(uint32_t w);

// isa.h (namespace isa)
Insn  decode(uint32_t w);
uint32_t encode(const Insn&);
bool  validate(const Insn&, std::string* why = nullptr);
```

`AsmResult` contains `words`, `first_org` (−1 if there is no leading `.org`), and `ok`.

All constraints (constant ranges, ROM size, `CALL_BY_REG`) are collected at the top of `isa.h`. When the architecture changes, only that place and the code tables are edited.

---

## 11. Self-test

```
make isa-selftest
```

It checks:
- reference words from `rtl/bootloader.hex` (`MOV R0, DATA0` → `01000B00`, `SUB R0, #2` → `6180D002`, `JMP.NE 3` → `44001003`, …);
- about 60 lines that must be rejected and about 40 that must assemble;
- labels, `.org`, `.word`, both spellings of the `L` bit;
- the round trip `disasm` → `asm` on ~1.9 million words (random words and random valid instructions): there must be 0 mismatches.

Expected result: `ALL CHECKS PASSED`.

---

## 12. Changes compared to the old version

- Errors are no longer silently skipped: a message with the line number and an empty result.
- `FLUSH` takes a **channel number** 0…3 (previously a bitmask number).
- `HALT` ≤ 127, `IRQ` ≤ 255.
- `CMP` and `TST` work.
- The `#imm` range in ALU instructions is extended to 2047 (`OR R1, #0x700`).
- Added modifiers `!w`, `!f`, `!nf`, the `.L` suffix, case-insensitivity, `0b…`.
- `L` on `CALL` is rejected.
- `.org 0` reports `out_org = 0`.