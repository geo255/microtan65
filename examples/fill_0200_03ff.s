; fill_0200_03ff.s
; Example for asm6502.py
; Fills memory from $0200 to $03FF with $20.
; Code starts at $0400 and ends with BRK.

.org $0400

start:
    ldx #$00

loop:
    lda #$20
    sta $0200,x
    sta $0300,x
    inx
    bne loop
    brk
