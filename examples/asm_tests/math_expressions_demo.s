; math_expressions_demo.s
; Exercises arithmetic/bitwise expression support.

.org $0A00

.equ SCREEN_BASE, $0200
.equ WIDTH,      32
.equ HALF,       WIDTH / 2
.equ MASK,       (1 << 5) - 1

Start:
    lda #(HALF + 1)
    sta SCREEN_BASE + 3

    lda #((10 * 3) / 4)
    sta SCREEN_BASE + (WIDTH / 2)

    lda #(MASK & $0F)
    sta SCREEN_BASE + ((3 * WIDTH) + 1)

    brk

MathTable:
    .byte 1 + 2, 8 / 3, (5 << 2), (255 & 15), (12 ^ 10), (~0) & $FF
    .word SCREEN_BASE + (WIDTH * 3), Start + (MathTable - Start)
