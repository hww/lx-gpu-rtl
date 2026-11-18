.org 0x20
        ; --- Канал 0 (спрайт 8bpp, страница 2) ---
        LDI     R5, #0x0302      ; FMT=3 (8bpp), PAGE=2
        MOV     PAGE0, R5
        LDI     R5, #1           ; STEP0 = 16 (1 слово)
        MOV     STEP0, R5
        MOV     IDXH0, #0
        LDI     R4, #0x0022      ; IDX0 = 0x22 (начало спрайта в RAM)
        MOV     IDX0, R4

        ; --- Канал 1 (framebuffer 8bpp, страница 2) ---
        LDI     R5, #0x0302      ; FMT=3 (8bpp), PAGE=2
        MOV     PAGE1, R5
        LDI     R5, #1           ; STEP1 = 1
        MOV     STEP1, R5
        MOV     IDXH1, #0
        MOV     IDX1, #0         ; IDX1 = 0 (куда пишем)

        ; --- Цикл ---
        LDI     R0, #256          ; 128 слов (256 пикселей / 2)
draw_loop:
        MOV     R2, DATA0
        MOV     DATA1, R2
        SUB     R0, #1
        JMP.NE  draw_loop
        FLUSH 3                  ; Сброс каналов 0 и 1 (маска 0011)
        HALT