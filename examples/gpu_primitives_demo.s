; gpu_primitives_demo.s
; Demonstrates GPU primitive drawing commands in microtan65.
;
; Run this at $0400, then switch emulator display mode to "GPU".

.org $0400
    jmp Start

.include "includes/microtan_hw_labels.inc"

; GPU register map (base $BF00)
.equ GPU_BASE,          $BF00
.equ GPU_COLOR,         GPU_BASE + $00
.equ GPU_P1,            GPU_BASE + $01
.equ GPU_P2,            GPU_BASE + $02
.equ GPU_P3,            GPU_BASE + $03
.equ GPU_P4,            GPU_BASE + $04
.equ GPU_P5,            GPU_BASE + $05
.equ GPU_P6,            GPU_BASE + $06
.equ GPU_WRITE_MASK,    GPU_BASE + $7B
.equ GPU_DISPLAY_MASK,  GPU_BASE + $7C
.equ GPU_STATUS,        GPU_BASE + $7E
.equ GPU_OP,            GPU_BASE + $7F

; GPU opcodes
.equ GPU_OP_SET_PIXEL,      $00
.equ GPU_OP_DRAW_LINE,      $10
.equ GPU_OP_DRAW_LINE_TO,   $11
.equ GPU_OP_DRAW_TRI,       $20
.equ GPU_OP_FILL_TRI,       $21
.equ GPU_OP_DRAW_RECT,      $30
.equ GPU_OP_FILL_RECT,      $31
.equ GPU_OP_DRAW_ELLIPSE,   $40
.equ GPU_OP_FILL_ELLIPSE,   $41

Start:
    ; Enable GPU drawing/display masks.
    lda #$0F
    sta GPU_WRITE_MASK
    lda #$01
    sta GPU_DISPLAY_MASK

    ; Clear screen to black using a full-screen filled rectangle.
    lda #$00
    sta GPU_COLOR
    lda #$00
    sta GPU_P1      ; x1
    sta GPU_P2      ; y1
    lda #$FF
    sta GPU_P3      ; x2
    sta GPU_P4      ; y2
    lda #GPU_OP_FILL_RECT
    sta GPU_OP

    ; White crosshair at center.
    lda #$0F
    sta GPU_COLOR
    lda #$80
    sta GPU_P1
    sta GPU_P3
    lda #$10
    sta GPU_P2
    lda #$F0
    sta GPU_P4
    lda #GPU_OP_DRAW_LINE
    sta GPU_OP
    lda #$10
    sta GPU_P1
    lda #$F0
    sta GPU_P3
    lda #$80
    sta GPU_P2
    sta GPU_P4
    lda #GPU_OP_DRAW_LINE
    sta GPU_OP

    ; Red line + line-to chain.
    lda #$22
    sta GPU_COLOR
    lda #$08
    sta GPU_P1
    lda #$20
    sta GPU_P2
    lda #$60
    sta GPU_P3
    lda #$40
    sta GPU_P4
    lda #GPU_OP_DRAW_LINE
    sta GPU_OP
    lda #$B8
    sta GPU_P3
    lda #$20
    sta GPU_P4
    lda #GPU_OP_DRAW_LINE_TO
    sta GPU_OP
    lda #$F0
    sta GPU_P3
    lda #$70
    sta GPU_P4
    lda #GPU_OP_DRAW_LINE_TO
    sta GPU_OP

    ; Cyan rectangle outline.
    lda #$3C
    sta GPU_COLOR
    lda #$10
    sta GPU_P1
    lda #$30
    sta GPU_P2
    lda #$70
    sta GPU_P3
    lda #$A0
    sta GPU_P4
    lda #GPU_OP_DRAW_RECT
    sta GPU_OP

    ; Blue filled rectangle.
    lda #$14
    sta GPU_COLOR
    lda #$88
    sta GPU_P1
    lda #$38
    sta GPU_P2
    lda #$E8
    sta GPU_P3
    lda #$90
    sta GPU_P4
    lda #GPU_OP_FILL_RECT
    sta GPU_OP

    ; Yellow triangle outline.
    lda #$3A
    sta GPU_COLOR
    lda #$20
    sta GPU_P1
    lda #$D0
    sta GPU_P2
    lda #$60
    sta GPU_P3
    lda #$98
    sta GPU_P4
    lda #$A0
    sta GPU_P5
    lda #$D0
    sta GPU_P6
    lda #GPU_OP_DRAW_TRI
    sta GPU_OP

    ; Magenta filled triangle.
    lda #$2D
    sta GPU_COLOR
    lda #$B0
    sta GPU_P1
    lda #$C8
    sta GPU_P2
    lda #$E8
    sta GPU_P3
    lda #$A8
    sta GPU_P4
    lda #$F8
    sta GPU_P5
    lda #$E8
    sta GPU_P6
    lda #GPU_OP_FILL_TRI
    sta GPU_OP

    ; Green ellipse outline.
    lda #$2A
    sta GPU_COLOR
    lda #$18
    sta GPU_P1
    lda #$08
    sta GPU_P2
    lda #$98
    sta GPU_P3
    lda #$58
    sta GPU_P4
    lda #GPU_OP_DRAW_ELLIPSE
    sta GPU_OP

    ; Orange filled ellipse.
    lda #$2E
    sta GPU_COLOR
    lda #$A0
    sta GPU_P1
    lda #$08
    sta GPU_P2
    lda #$F8
    sta GPU_P3
    lda #$58
    sta GPU_P4
    lda #GPU_OP_FILL_ELLIPSE
    sta GPU_OP

    ; Single white pixel to show set-pixel primitive.
    lda #$0F
    sta GPU_COLOR
    lda #$80
    sta GPU_P1
    sta GPU_P2
    lda #GPU_OP_SET_PIXEL
    sta GPU_OP

    ; Optional: read status (not used in this demo)
    lda GPU_STATUS

    brk
