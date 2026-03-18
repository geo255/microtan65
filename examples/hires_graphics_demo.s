; hires_graphics_demo.s
; Demo program for lib/hires_graphics_lib.s
;
; Before running, select Tangerine hi-res display mode in the emulator menu.


.org $0400
	jmp Start
.include "includes/microtan_hw_labels.inc"
.include "../lib/hires_graphics_lib.s"
Start:
    ; Clear to black.
    lda #$00
    jsr HRG_ClearScreenA

    ; White center pixel.
    ldx #$80
    ldy #$80
    lda #$0F
    jsr HRG_SetPixelXYA

    ; Red diagonal line (top-left to bottom-right).
    lda #$01
    sta HRG_COLOR
    lda #$00
    sta HRG_X0
    sta HRG_Y0
    lda #$FF
    sta HRG_X1
    sta HRG_Y1
    jsr HRG_DrawLine

    ; Bright green box outline.
    lda #$0A
    sta HRG_COLOR
    lda #$10
    sta HRG_X0
    lda #$20
    sta HRG_Y0
    lda #$F0
    sta HRG_X1
    lda #$E0
    sta HRG_Y1
    jsr HRG_DrawRect

    ; Blue filled box.
    lda #$04
    sta HRG_COLOR
    lda #$20
    sta HRG_X0
    lda #$30
    sta HRG_Y0
    lda #$70
    sta HRG_X1
    lda #$90
    sta HRG_Y1
    jsr HRG_FillRect

    ; Yellow triangle outline.
    lda #$03
    sta HRG_COLOR
    lda #$18
    sta HRG_X0
    lda #$D8
    sta HRG_Y0
    lda #$80
    sta HRG_X1
    lda #$40
    sta HRG_Y1
    lda #$E8
    sta HRG_X2
    lda #$D8
    sta HRG_Y2
    jsr HRG_DrawTriangle

    ; Magenta filled triangle.
    lda #$0D
    sta HRG_COLOR
    lda #$30
    sta HRG_X0
    lda #$70
    sta HRG_Y0
    lda #$C0
    sta HRG_X1
    lda #$70
    sta HRG_Y1
    lda #$80
    sta HRG_X2
    lda #$C8
    sta HRG_Y2
    jsr HRG_FillTriangle

    ; Cyan ellipse outline.
    lda #$06
    sta HRG_COLOR
    lda #$08
    sta HRG_X0
    lda #$08
    sta HRG_Y0
    lda #$78
    sta HRG_X1
    lda #$58
    sta HRG_Y1
    jsr HRG_DrawEllipse

    ; Bright red filled ellipse.
    lda #$09
    sta HRG_COLOR
    lda #$90
    sta HRG_X0
    lda #$20
    sta HRG_Y0
    lda #$F8
    sta HRG_X1
    lda #$88
    sta HRG_Y1
    jsr HRG_FillEllipse

    brk
