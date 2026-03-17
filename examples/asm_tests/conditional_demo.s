; conditional_demo.s
; Demonstrates .if/.elif/.else/.endif and .ifdef/.ifndef.

.org $0B00

.equ ENABLE_DRAW, 1
.equ TARGET,      2

.if ENABLE_DRAW == 1
Start:
    lda #$41
.else
Start:
    lda #$00
.endif
    sta $0200

.if TARGET == 1
    lda #$11
.elif TARGET == 2
    lda #$22
.else
    lda #$FF
.endif
    sta $0201

.if 0
    lda #$99
.elif 1
    lda #$55
.else
    lda #$11
.endif
    sta $0202

.if 1
    .if TARGET != 0
        lda #$77
    .else
        lda #$00
    .endif
.endif
    sta $0203

    brk
