.org 0x20
start:
    LDI R0, #3      ; Счетчик = 3
loop:
    SUB R0, #1      ; R0 = R0 - 1 (должен выставить Z, когда R0=0)
    JMP.NE loop     ; Если Z=0 (R0 != 0), идем на loop
    LDI R1, #0xAA   ; Маркер успеха (если цикл завершился)
    HALT