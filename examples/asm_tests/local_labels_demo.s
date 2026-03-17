; local_labels_demo.s
; Demonstrates re-usable local labels under separate global labels.

.org $0700

FirstLoop:
    ldx #$03
@loop:
    dex
    bne @loop

SecondLoop:
    ldy #$03
@loop:
    dey
    bne @loop

    brk
