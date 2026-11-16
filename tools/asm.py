#!/usr/bin/env python3
"""Minimal assembler for LX-GPU ISA (see isa.yaml).

Usage:  python3 tools/asm.py input.asm > output.hex
        python3 tools/asm.py --raw input.asm    # print hex without comments

Encodings (32-bit):
  L        [31]
  J        [30]
  I        [29]
  DST_CODE [28:23]
  WRITE    [22]
  COND     [21:18]
  F        [17]
  OP       [16:12]
  SRC      [11:6]   (reg-reg, ctrl-jmpr)
  R        [16]     (ctrl: 1 = JMP/CALL by reg)   -- overlaps OP[4], ok because CTRL
  SX       [11]     (reg-imm11)
  IMM11    [10:0]   (reg-imm11)
  ADDR     [11:0]   (ctrl-jmp)
  IMM16    [15:0]   (ldi16)

Formats (J, I):
  00  reg-reg
  01  ldi16
  10  ctrl (R=0 immediate, R=1 register for JMP/CALL)
  11  reg-imm11
"""
import sys, re

# ---------------- register / operand pool ----------------
REG = {f"R{i}": i for i in range(32)}
REG.update({
    "ZERO": 32, "FLAGS": 33, "BUSY": 34, "SHADOW": 35,
    "LOOP.CNT": 36, "LOOP.TGT": 37,
    "ROML": 38, "ROMH": 39, "ROMA": 40, "MHI": 41,
    "DATA0": 44, "DATA1": 45, "DATA2": 46, "DATA3": 47,
})
for ch in range(4):
    REG[f"IDX{ch}"]  = 48 + ch
    REG[f"IDXH{ch}"] = 52 + ch
    REG[f"STEP{ch}"] = 56 + ch
    REG[f"PAGE{ch}"] = 60 + ch

# ---------------- ALU ops ----------------
# cls = op[4:3], fn = op[2:1], flag = op[0]
ALU = {
    "MOV":  0b00_00_0, "MVN":  0b00_00_1,
    "AND":  0b00_01_0, "BIC":  0b00_01_1,
    "OR":   0b00_10_0, "ORN":  0b00_10_1,
    "XOR":  0b00_11_0, "XNOR": 0b00_11_1,
    "ADD":  0b01_00_0, "ADC":  0b01_01_0,
    "SUB":  0b01_10_1, "SBC":  0b01_11_1,
    "ROR":  0b10_00_0, "SHR":  0b10_01_0, "SAR": 0b10_10_0,
    "MUL":  0b11_00_0,
    "TST":  0b11_10_0,   # = AND with write=0, f=1
    "CMP":  0b11_11_0,   # = SUB with write=0, f=1
}

# ---------------- CTRL ops (spec: ctrl_ops) ----------------
CTRL = {"NOP":0, "JMP":1, "CALL":2, "RET":3, "HALT":4, "IRQ":5, "FLUSH":6}

# ---------------- conditions ----------------
COND = {"AL":0,"EQ":1,"NE":2,"CS":3,"CC":4,"MI":5,"PL":6,
        "VS":7,"VC":8,"HI":9,"LS":10,"GE":11,"LT":12,"GT":13,"LE":14}

CHANNELS = {"CH0":0, "CH1":1, "CH2":2, "CH3":3}

# ---------------- encoder ----------------
def encode(l, j, i, dst, write, cond, f, op, src=0, sx=0, imm11=0, imm16=0):
    """Generic 32-bit encoder; j/i are bits 30/29."""
    imm11 &= 0x7FF
    imm16 &= 0xFFFF
    src &= 0x3F
    return (
        ((l & 1) << 31) |
        ((j & 1) << 30) |
        ((i & 1) << 29) |
        ((cond & 0xF) << 25) |
        ((write & 1) << 24) |
        ((f & 1) << 23) |
        ((dst & 0x3F) << 17) |
        ((op & 0x1F) << 12) |
        ((src & 0x3F) << 6) |
        ((sx & 1) << 11) |
        (imm11 & 0x7FF) |
        # ldi16 uses imm16 in [15:0], overlaps SX/imm11; caller ensures zeroing
        (imm16 & 0xFFFF)
    )

def enc_reg_reg(l, dst, write, cond, f, op, src):
    return encode(l=l, j=0, i=0, dst=dst, write=write, cond=cond, f=f,
                  op=op, src=src)

def enc_reg_imm11(l, dst, write, cond, f, op, sx, imm11):
    return encode(l=l, j=1, i=1, dst=dst, write=write, cond=cond, f=f,
                  op=op, sx=sx, imm11=imm11)

def enc_ldi16(l, dst, write, cond, imm16):
    return encode(l=l, j=0, i=1, dst=dst, write=write, cond=cond, f=0,
                  op=0, imm16=imm16)

def enc_ctrl(l, cond, op, addr=0, r=0, src=0):
    """CTRL: if r=1, op[4] (bit16) is set and SRC holds the register.
       ADDR[11:0] holds immediate (when r=0) or channel/code (FLUSH/HALT/IRQ)."""
    if r:
        # bit16 = R = 1
        return encode(l=l, j=1, i=0, dst=0, write=0, cond=cond, f=0,
                      op=op, src=src) | (1 << 16)
    return encode(l=l, j=1, i=0, dst=0, write=0, cond=cond, f=0,
                  op=op, src=0) | (addr & 0xFFF)

# ---------------- parser helpers ----------------
def strip_comment(line):
    return re.sub(r";.*", "", line).strip()

def parse_int(tok):
    tok = tok.strip()
    neg = tok.startswith("-")
    if neg: tok = tok[1:]
    v = int(tok, 0)
    return -v if neg else v

def is_reg(tok):
    return tok in REG

def reg_code(tok):
    if tok not in REG:
        raise ValueError(f"unknown operand: {tok}")
    return REG[tok]

def sign_extend_ok(val):
    return -1024 <= val <= 1023

# ---------------- line assembler ----------------
def assemble_line(text, lineno, labels, warnings):
    text = strip_comment(text)
    if not text:
        return None

    l_flag = 0
    m = re.match(r"^L\s+(\S+)(.*)$", text)
    if m:
        l_flag = 1
        text = (m.group(1) + m.group(2)).strip()

    # split mnemonic[.cond] and args
    parts = text.split(None, 1)
    mn = parts[0].upper()
    rest = parts[1].strip() if len(parts) > 1 else ""

    cond = 0
    if "." in mn:
        base, cnd = mn.split(".", 1)
        mn = base
        if cnd not in COND:
            raise ValueError(f"line {lineno}: unknown cond {cnd}")
        cond = COND[cnd]

    args = [a.strip() for a in rest.split(",")] if rest else []

    # ---- .word directive ----
    if mn == ".WORD":
        if len(args) != 1:
            raise ValueError(f"line {lineno}: .word expects 1 arg")
        return parse_int(args[0]) & 0xFFFFFFFF

    # ---- CTRL ----
    if mn in CTRL:
        op = CTRL[mn]

        if mn in ("JMP", "CALL"):
            if len(args) != 1:
                raise ValueError(f"line {lineno}: {mn} expects 1 arg")
            tok = args[0]
            if is_reg(tok):
                if mn == "CALL":
                    warnings.append(f"line {lineno}: CALL with register - DST ignored, SHADOW overwritten by hardware")
                return enc_ctrl(l=l_flag, cond=cond, op=op, r=1,
                                src=reg_code(tok))
            else:
                if tok in labels:
                    addr = labels[tok]
                else:
                    addr = parse_int(tok)
                if not (0 <= addr <= 0xFFF):
                    raise ValueError(f"line {lineno}: {mn} addr {addr} out of 12-bit range")
                return enc_ctrl(l=l_flag, cond=cond, op=op, addr=addr)

        if mn in ("HALT", "IRQ"):
            code = 0
            if args:
                code = parse_int(args[0]) if args[0] not in labels else labels[args[0]]
            return enc_ctrl(l=l_flag, cond=cond, op=op, addr=code)

        if mn == "FLUSH":
            if len(args) != 1:
                raise ValueError(f"line {lineno}: FLUSH expects 1 channel")
            tok = args[0]
            ch = CHANNELS[tok] if tok in CHANNELS else parse_int(tok)
            return enc_ctrl(l=l_flag, cond=cond, op=op, addr=ch)

        if mn in ("NOP", "RET"):
            if args:
                raise ValueError(f"line {lineno}: {mn} takes no args")
            return enc_ctrl(l=l_flag, cond=cond, op=op)

        raise ValueError(f"line {lineno}: unhandled ctrl {mn}")

    # ---- LDI16 ----
    if mn == "LDI":
        if len(args) != 2:
            raise ValueError(f"line {lineno}: LDI expects dst, #imm16")
        dst = reg_code(args[0])
        if not args[1].startswith("#"):
            raise ValueError(f"line {lineno}: LDI immediate must be #...")
        imm = parse_int(args[1][1:])
        if not (0 <= imm <= 0xFFFF):
            raise ValueError(f"line {lineno}: LDI imm16 out of range")
        return enc_ldi16(l=l_flag, dst=dst, write=1, cond=cond, imm16=imm)

    # ---- ALU ----
    if mn not in ALU:
        raise ValueError(f"line {lineno}: unknown mnemonic {mn}")

    op = ALU[mn]
    write = 0 if mn in ("TST", "CMP") else 1
    f = 0 if mn in ("MOV", "MVN") else 1
    if mn in ("TST", "CMP"):
        f = 1

    if len(args) != 2:
        raise ValueError(f"line {lineno}: {mn} expects 2 args, got {args}")

    dst = reg_code(args[0])
    src_tok = args[1]

    if src_tok.startswith("#"):
        val = parse_int(src_tok[1:])
        if not sign_extend_ok(val):
            raise ValueError(f"line {lineno}: imm11 {val} out of signed range -1024..1023")
        if val < 0:
            sx = 1
            imm = val & 0x7FF
        else:
            sx = 0
            imm = val
        return enc_reg_imm11(l=l_flag, dst=dst, write=write, cond=cond,
                             f=f, op=op, sx=sx, imm11=imm)
    else:
        src = reg_code(src_tok)
        return enc_reg_reg(l=l_flag, dst=dst, write=write, cond=cond,
                           f=f, op=op, src=src)

# ---------------- two-pass assembler ----------------
def assemble(lines):
    labels = {}
    insns = []
    for i, raw in enumerate(lines, 1):
        line = strip_comment(raw)
        if not line:
            continue
        m = re.match(r"^(\w+):\s*(.*)$", line)
        if m:
            labels[m.group(1)] = len(insns)
            line = m.group(2).strip()
            if not line:
                continue
        insns.append((i, line))

    out = []
    warnings = []
    for lineno, text in insns:
        w = assemble_line(text, lineno, labels, warnings)
        out.append((w, text))
    return out, warnings

def main():
    pack_mode = False
    pack_start = 0x0020
    argv = sys.argv[1:]
    raw = False
    pad = 0
    while argv and argv[0].startswith("--"):
        if argv[0] == "--raw":
            raw = True; argv = argv[1:]
        elif argv[0] == "--pad":
            pad = int(argv[1], 0); argv = argv[2:]
        elif argv[0] == "--pack":
            pack_mode = True; argv = argv[1:]
        elif argv[0] == "--start":
            pack_start = int(argv[1], 0); argv = argv[2:]
        else:
            print(f"error: unknown option {argv[0]}", file=sys.stderr)
            sys.exit(1)

    if len(argv) != 1:
        print(f"usage: {sys.argv[0]} [--raw] [--pad N] [--pack] [--start ADDR] <file.asm>",
              file=sys.stderr)
        sys.exit(1)

    with open(argv[0]) as fp:
        words, warnings = assemble(fp.readlines())

    for w in warnings:
        print(f"WARNING: {w}", file=sys.stderr)

    if pack_mode:
        # package: SIZE (even word count), START, DATA*[lo,hi]...
        pkg = [len(words) * 2 & 0xFFFF, pack_start & 0xFFFF]
        for w, _ in words:
            pkg.append(w & 0xFFFF)
            pkg.append((w >> 16) & 0xFFFF)
        for v in pkg:
            print(f"{v:04X}")
        return

    if pad and len(words) > pad:
        print(f"error: {len(words)} words exceed --pad {pad}", file=sys.stderr)
        sys.exit(1)

    for w, text in words:
        if raw:
            print(f"{w:08X}")
        else:
            print(f"{w:08X}   ; {text}")

    if pad:
        for _ in range(pad - len(words)):
            print("00000000")

if __name__ == "__main__":
    try:
        main()
    except BrokenPipeError:
        # stdout closed (e.g. `| head`); exit quietly
        import os, sys
        devnull = os.open(os.devnull, os.O_WRONLY)
        os.dup2(devnull, sys.stdout.fileno())
        sys.exit(0)