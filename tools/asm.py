#!/usr/bin/env python3
"""
Мини-ассемблер под LX-GPU ISA.
Формат insn (32 бита):
  [31]    l
  [30:29] imm_class  (00=reg-reg, 10=ctrl, 11=reg-imm12)
  [28:24] dst
  [23]    ds
  [22:19] cond
  [18]    f
  [17:13] reg_op
  [12:7]  src
  [11:0]  imm12
"""
import sys, re

R = {f"R{i}": i for i in range(32)}
R["ZERO"]      = 32
R["FLAGS"]     = 33
R["SHADOW"]    = 34
R["LOOP.CNT"]  = 35
R["LOOP.TGT"]  = 36
R["BUSY"]      = 37
R["MHI"]       = 38
R["DATA0"]     = 40
R["DATA1"]     = 41
R["DATA2"]     = 42
R["DATA3"]     = 43
for ch in range(4):
    R[f"CH{ch}.IDX"]       = 48 + 4*ch + 0
    R[f"CH{ch}.IDXH"]      = 48 + 4*ch + 1
    R[f"CH{ch}.STEP"]      = 48 + 4*ch + 2
    R[f"CH{ch}.MODE_PAGE"] = 48 + 4*ch + 3

ALU = {
    "MOV": 0b00_00_0, "MVN": 0b00_00_1,
    "AND": 0b00_01_0, "BIC": 0b00_01_1,
    "OR":  0b00_10_0, "ORN": 0b00_10_1,
    "XOR": 0b00_11_0, "XNOR":0b00_11_1,
    "ADD": 0b01_00_0, "ADC": 0b01_01_0,
    "SUB": 0b01_10_0, "SBC": 0b01_11_0,
    "ROR": 0b10_00_0, "SHR": 0b10_01_0, "SAR": 0b10_10_0,
    "MUL": 0b11_00_0, "TST": 0b11_10_0, "CMP": 0b11_11_0,
}

CTRL = {"NOP":0, "JMP":1, "CALL":2, "RET":3, "JMPR":4,
        "HALT":5, "IRQ":6, "FLUSH":7}

COND = {"AL":0,"EQ":1,"NE":2,"CS":3,"CC":4,"MI":5,"PL":6,
        "VS":7,"VC":8,"HI":9,"LS":10,"GE":11,"LT":12,"GT":13,"LE":14}

def encode_insn(l=0, imm_class=0, dst_code=0, cond=0, f=0, op=0, src=0, imm12=0):
    dst = dst_code & 0x1F
    ds  = (dst_code >> 5) & 1
    return (
        ((l & 1) << 31) |
        ((imm_class & 3) << 29) |
        ((dst & 0x1F) << 24) |
        ((ds & 1) << 23) |
        ((cond & 0xF) << 19) |
        ((f & 1) << 18) |
        ((op & 0x1F) << 13) |
        ((src & 0x3F) << 7) |
        (imm12 & 0xFFF)
    )

def parse_reg(tok):
    tok = tok.strip().rstrip(",")
    if tok in R: return R[tok]
    raise ValueError(f"unknown operand: {tok}")

def parse_imm(tok):
    return int(tok.strip().rstrip(","), 0)

def resolve(tok, labels):
    """Если tok — метка, вернуть её номер. Иначе int(tok, 0)."""
    tok = tok.strip().rstrip(",")
    if tok in labels:
        return labels[tok]
    return int(tok, 0)

def assemble_line(line, lineno, labels):
    line = re.sub(r";.*", "", line).strip()
    if not line:
        return None
    l_flag = 0
    m = re.match(r"^L\s+(\w+\.?\w*)(.*)$", line)
    if m:
        l_flag = 1
        line = m.group(1) + m.group(2)
    parts = line.split(None, 1)
    mn = parts[0].upper()
    rest = parts[1] if len(parts) > 1 else ""
    cond = 0
    if "." in mn:
        mn, cnd = mn.split(".", 1)
        cond = COND[cnd]
    args = [a.strip() for a in rest.split(",")] if rest else []

    if mn in CTRL:
        op = CTRL[mn]
        imm = 0
        if args:
            imm = resolve(args[-1], labels)
        return encode_insn(l=l_flag, imm_class=0b10, cond=cond, op=op, imm12=imm)

    if mn not in ALU:
        raise ValueError(f"line {lineno}: unknown mnemonic {mn}")

    op = ALU[mn]
    f = 0 if mn in ("MOV", "MVN") else 1
    if len(args) != 2:
        raise ValueError(f"line {lineno}: {mn} expects 2 args, got {args}")

    dst_code = parse_reg(args[0])
    src_tok = args[1]
    if src_tok.startswith("#"):
        return encode_insn(l=l_flag, imm_class=0b11, dst_code=dst_code,
                           cond=cond, f=f, op=op, src=0,
                           imm12=parse_imm(src_tok[1:]))
    else:
        return encode_insn(l=l_flag, imm_class=0b00, dst_code=dst_code,
                           cond=cond, f=f, op=op,
                           src=parse_reg(src_tok), imm12=0)

def assemble(lines):
    labels = {}
    insns = []
    for i, line in enumerate(lines, 1):
        l = re.sub(r";.*", "", line).strip()
        if not l:
            continue
        m = re.match(r"^(\w+):\s*(.*)$", l)
        if m:
            labels[m.group(1)] = len(insns)
            l = m.group(2).strip()
            if not l:
                continue
        insns.append((i, l))
    out = []
    for lineno, text in insns:
        out.append((assemble_line(text, lineno, labels), text))
    return out

def main():
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} <file.asm>", file=sys.stderr)
        sys.exit(1)
    with open(sys.argv[1]) as fp:
        words = assemble(fp.readlines())
    for w, text in words:
        print(f"{w:08X}   ; {text}")

if __name__ == "__main__":
    main()