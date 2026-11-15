        MOV     R0, DATA0
        MOV     R1, DATA0
        MOV     ROMA, R1
copy_loop:
        MOV     R2, DATA0
        MOV     ROML, R2
        MOV     R3, DATA0
        MOV     ROMH, R3
        SUB     R0, #2
        JMP.NE  copy_loop
        JMP     R1