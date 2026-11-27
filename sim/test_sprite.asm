; test_sprite.asm — v2, без прозрачности
; Копирует 16x16 8bpp спрайт из pixel 0x0400 в экран 320x200 8bpp по (100,100).
; Тело цикла — 2 инструкции: чтение пикселя + запись.
; XI1 двигается через DXI1=1, R3 — только между строками (+320).
.org 0x20

; ---- CH0: спрайт 16x16 @ pixel 0x0400, 8bpp, linear ----
        MOV     BANK, #4
        LDI     R5, #0x0100         ; FMT=1 (8bpp), PAGE=0
        MOV     W48, R5             ; MODE_PAGE0
        MOV     BANK, #1
        LDI     R5, #1
        MOV     W48, R5             ; DXI0 = 1
        MOV     BANK, #0
        LDI     R4, #0x0400
        MOV     W48, R4             ; XI0 = 0x0400
        MOV     W52, #0             ; YI0 = 0

; ---- CH1: экран @ (100,100), 8bpp, linear ----
        MOV     BANK, #4
        LDI     R5, #0x0100
        MOV     W49, R5             ; MODE_PAGE1
        MOV     BANK, #1
        LDI     R5, #1
        MOV     W49, R5             ; DXI1 = 1
        MOV     BANK, #0
; ---- Blending ---
        MOV     BANK, #3            ; PEN
        LDI     R5, #0x0800         ; ALU_CTL: REL=NE, CS=A?K, OP=PASS
        MOV     W57, R5             ; ALU_CTL1
        LDI     R5, #0
        MOV     W53, R5             ; KEY1 = 0
        MOV     BANK, #0
; ---- Инициализация цикла ----
        LDI     R3, #0x8564         ; 0x800 (bg base) + 100*320 + 100
        LDI     R6, #320            ; ширина экрана
        LDI     R1, #16             ; 16 строк
        LDI     R0, #16             ; 16 пикселей в строке
        LDI     R4, #col_loop
        MOV     LOOP_TGT, R4

row_loop:
        MOV     W49, R3             ; XI1 = начало строки
        MOV     W53, #0             ; YI1 = 0
        MOV     LOOP_CNT, R0        ; цикл 16 пикселей

col_loop:
        MOV     R2, DATA0           ; прочитать пиксель спрайта
      L MOV     DATA1, R2           ; записать в экран, L = конец цикла

        ADD     R3, R6              ; R3 += 320 -> следующая строка
        SUB     R1, #1
        JMP.NE  row_loop

        FLUSH   1
        HALT