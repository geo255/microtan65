; include_demo_main.s
; Demonstrates .include and nested includes.

.org $0400
	jmp Start

.include "include_demo_common.inc"

Start:
    ldx #$00
@copy_loop:
    lda Message, X
    beq @done
    sta SCREEN_BASE, X
    inx
    bne @copy_loop

@done:
    jsr FillTopRow
    brk 
