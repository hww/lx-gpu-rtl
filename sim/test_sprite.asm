        .org 0x20           ; Программа начнется с адреса 0x20

draw_start:
        ; --- Канал 0 (источник: спрайт) ---
        LDI     R5, #0x0200
        MOV     PAGE0, R5
        LDI     R5, #16
        MOV     STEP0, R5
        MOV     IDXH0, #0
        LDI     R4, #0x0040
        MOV     IDX0, R4

        ; --- Канал 1 (framebuffer) ---
        LDI     R5, #0x0201
        MOV     PAGE1, R5
        LDI     R5, #16
        MOV     STEP1, R5
        MOV     IDXH1, #0
        MOV     IDX1, #0

        ; --- Цикл ---
        LDI     R0, #256
draw_loop:
        MOV     R2, DATA0
        MOV     DATA1, R2
        SUB     R0, #1
        JMP.NE  draw_loop
        HALT

        .org 0x40           ; Спрайт лежит с адреса 0x40
        ; Данные спрайта (16x16 4bpp = 64 слова по 16 бит = 32 слова по 32 бита)
        ; Я добавлю сюда тестовый узор "шахматка"
        .word 0xFFFF, 0xFFFF, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0xFFFF
        ; ... и так далее (для теста)