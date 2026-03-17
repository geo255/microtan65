; include_demo_common.inc
; Common constants + routine, and a nested include.

.equ SCREEN_BASE, $0200
.equ ROW_WIDTH,   $20
.equ FILL_VALUE,  $20

.include "include_demo_tables.inc"

FillTopRow:
    ldx #$00
    lda #FILL_VALUE
@loop:
    sta SCREEN_BASE, X
    inx
    cpx #ROW_WIDTH
    bne @loop
    rts
