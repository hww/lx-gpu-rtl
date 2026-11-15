        ; CH0: 4bpp, PAGE=0, STEP=1, IDX=0x0400
        MOV     PAGE0, #0x0200        ; FMT=2 (4bpp), PAGE=0
        MOV     STEP0, #1
        MOV     IDXH0, #0
        LDI     R4, #0x0400
        MOV     IDX0, R4

        ; read 4 pixels from DATA0
        MOV     R0, DATA0
        MOV     R1, DATA0
        MOV     R2, DATA0
        MOV     R3, DATA0

        HALT