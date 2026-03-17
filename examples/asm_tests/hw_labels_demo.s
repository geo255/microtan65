; hw_labels_demo.s
; Demonstrates AY8910 + VIA label include usage.

.include "../includes/microtan_hw_labels.inc"

.org $0400
Start:
    ; Configure VIA1 port B as output.
    lda #$FF
    sta VIA1_DDRB

    ; Example AY write: set mixer register index then write value.
    lda #AY_REG_ENABLE
    sta AY1_ADDR
    lda #$3F
    sta AY1_DATA

    ; Use a VIA2 timer register label.
    lda #$12
    sta VIA2_T1L_H

    brk
