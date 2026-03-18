; hires_graphics_lib.s
; 256x256 RGBI hi-res helpers for microtan65.
;
; Pixel memory model (as emulated):
;   - Hires VRAM window: $8000-$9FFF
;   - Write bank select to $FFFF:
;       1=Red, 2=Green, 3=Blue, 4=Intensity
;   - Pixel bit mapping in a byte: bit7=leftmost ... bit0=rightmost
;
; Colour format used by this library (A or HRG_COLOR):
;   bit0=Red, bit1=Green, bit2=Blue, bit3=Intensity
;
; Caller-owned fixed zero-page frame (default $A0-$D7).
; Routines favour speed and clobber A/X/Y unless noted.

.ifndef HIRES_BASE
.equ HIRES_BASE,         $8000
.equ HIRES_END,          $9FFF
.equ HIRES_BANK_SELECT,  $FFFF
.equ HIRES_BANK_RED,       $01
.equ HIRES_BANK_GREEN,     $02
.equ HIRES_BANK_BLUE,      $03
.equ HIRES_BANK_INTENSITY, $04
.endif

; Core call frame
.equ HRG_COLOR,    $A0
.equ HRG_X0,       $A1
.equ HRG_Y0,       $A2
.equ HRG_X1,       $A3
.equ HRG_Y1,       $A4
.equ HRG_X2,       $A5
.equ HRG_Y2,       $A6

; Pixel helpers
.equ HRG_PTRLO,    $A7
.equ HRG_PTRHI,    $A8
.equ HRG_MASK,     $A9
.equ HRG_INVMASK,  $AA
.equ HRG_TMP0,     $AB
.equ HRG_TMP1,     $AC
.equ HRG_TMP2,     $AD

; Line helpers
.equ HRG_DX,       $AE
.equ HRG_DY,       $AF
.equ HRG_ERR,      $B0
.equ HRG_STEPX,    $B1
.equ HRG_STEPY,    $B2
.equ HRG_CURX,     $B3
.equ HRG_CURY,     $B4

; Shape helpers
.equ HRG_MINX,     $B5
.equ HRG_MAXX,     $B6
.equ HRG_MINY,     $B7
.equ HRG_MAXY,     $B8
.equ HRG_COUNT,    $B9
.equ HRG_SAVE_X,   $BA
.equ HRG_SAVE_Y,   $BB

; Triangle edge walker state
.equ HRG_E0_X,     $BC
.equ HRG_E0_DX,    $BD
.equ HRG_E0_DY,    $BE
.equ HRG_E0_ERRL,  $BF
.equ HRG_E0_ERRH,  $C0
.equ HRG_E0_SX,    $C1

.equ HRG_E1_X,     $C2
.equ HRG_E1_DX,    $C3
.equ HRG_E1_DY,    $C4
.equ HRG_E1_ERRL,  $C5
.equ HRG_E1_ERRH,  $C6
.equ HRG_E1_SX,    $C7

; Ellipse helpers
.equ HRG_CX,       $C8
.equ HRG_CY,       $C9
.equ HRG_RX,       $CA
.equ HRG_RY,       $CB
.equ HRG_PX,       $CC
.equ HRG_PY,       $CD
.equ HRG_PX2,      $CE
.equ HRG_PY2,      $CF
.equ HRG_FIRSTX,   $D0
.equ HRG_FIRSTY,   $D1
.equ HRG_MUL_A,    $D2
.equ HRG_MUL_B,    $D3
.equ HRG_MUL_LO,   $D4
.equ HRG_MUL_HI,   $D5
.equ HRG_SIGN,     $D6
.equ HRG_INDEX,    $D7

; Internal temporary endpoint for line helpers (must not collide with HRG_COUNT).
.equ HRG_LINE_END, HRG_SIGN

; ------------------------------------------------------------
; Public API
; ------------------------------------------------------------

; A = RGBI nibble
HRG_ClearScreenA:
    sta HRG_COLOR

; HRG_COLOR = RGBI nibble
HRG_ClearScreen:
    cld
    lda HRG_COLOR
    and #$0F
    sta HRG_COLOR

    ; Red plane
    lda #HIRES_BANK_RED
    sta HIRES_BANK_SELECT
    lda HRG_COLOR
    and #$01
    beq @clr_red_0
    lda #$FF
    bne @clr_red_go
@clr_red_0:
    lda #$00
@clr_red_go:
    jsr HRG_FillAllHires

    ; Green plane
    lda #HIRES_BANK_GREEN
    sta HIRES_BANK_SELECT
    lda HRG_COLOR
    and #$02
    beq @clr_green_0
    lda #$FF
    bne @clr_green_go
@clr_green_0:
    lda #$00
@clr_green_go:
    jsr HRG_FillAllHires

    ; Blue plane
    lda #HIRES_BANK_BLUE
    sta HIRES_BANK_SELECT
    lda HRG_COLOR
    and #$04
    beq @clr_blue_0
    lda #$FF
    bne @clr_blue_go
@clr_blue_0:
    lda #$00
@clr_blue_go:
    jsr HRG_FillAllHires

    ; Intensity plane
    lda #HIRES_BANK_INTENSITY
    sta HIRES_BANK_SELECT
    lda HRG_COLOR
    and #$08
    beq @clr_i_0
    lda #$FF
    bne @clr_i_go
@clr_i_0:
    lda #$00
@clr_i_go:
    jsr HRG_FillAllHires
    rts

; X=x, Y=y, A=RGBI nibble
HRG_SetPixelXYA:
    cld
    sta HRG_COLOR
    stx HRG_X0
    sty HRG_Y0
    jsr HRG_PreparePixelPointer
    jmp HRG_WritePreparedPixel

; X=x, Y=y, A=RGBI nibble, preserves X/Y
HRG_SetPixelXYSafeA:
    stx HRG_SAVE_X
    sty HRG_SAVE_Y
    jsr HRG_SetPixelXYA
    ldx HRG_SAVE_X
    ldy HRG_SAVE_Y
    rts

; Uses HRG_X0,HRG_Y0,HRG_X1,HRG_Y1,HRG_COLOR
HRG_DrawLine:
    cld
    ; dx = abs(x1-x0), stepx = +1/-1
    lda HRG_X1
    sec
    sbc HRG_X0
    bcs @dx_pos
    eor #$FF
    clc
    adc #$01
    sta HRG_DX
    lda #$FF
    sta HRG_STEPX
    jmp @dy_calc
@dx_pos:
    sta HRG_DX
    lda #$01
    sta HRG_STEPX

@dy_calc:
    ; dy = abs(y1-y0), stepy = +1/-1
    lda HRG_Y1
    sec
    sbc HRG_Y0
    bcs @dy_pos
    eor #$FF
    clc
    adc #$01
    sta HRG_DY
    lda #$FF
    sta HRG_STEPY
    jmp @choose_major
@dy_pos:
    sta HRG_DY
    lda #$01
    sta HRG_STEPY

@choose_major:
    lda HRG_DY
    beq @horizontal
    lda HRG_DX
    beq @vertical
    cmp HRG_DY
    bcs @x_major
    jmp @y_major

@horizontal:
@horizontal_loop:
    jsr HRG_SetPixelFromZP
    lda HRG_X0
    cmp HRG_X1
    beq @line_done
    clc
    adc HRG_STEPX
    sta HRG_X0
    jmp @horizontal_loop

@vertical:
@vertical_loop:
    jsr HRG_SetPixelFromZP
    lda HRG_Y0
    cmp HRG_Y1
    beq @line_done
    clc
    adc HRG_STEPY
    sta HRG_Y0
    jmp @vertical_loop

@x_major:
    lda HRG_DX
    lsr A
    sta HRG_ERR
@x_major_loop:
    jsr HRG_SetPixelFromZP
    lda HRG_X0
    cmp HRG_X1
    beq @line_done
    clc
    adc HRG_STEPX
    sta HRG_X0

    lda HRG_ERR
    sec
    sbc HRG_DY
    sta HRG_ERR
    bcs @x_major_loop

    lda HRG_Y0
    clc
    adc HRG_STEPY
    sta HRG_Y0

    lda HRG_ERR
    clc
    adc HRG_DX
    sta HRG_ERR
    jmp @x_major_loop

@y_major:
    lda HRG_DY
    lsr A
    sta HRG_ERR
@y_major_loop:
    jsr HRG_SetPixelFromZP
    lda HRG_Y0
    cmp HRG_Y1
    beq @line_done
    clc
    adc HRG_STEPY
    sta HRG_Y0

    lda HRG_ERR
    sec
    sbc HRG_DX
    sta HRG_ERR
    bcs @y_major_loop

    lda HRG_X0
    clc
    adc HRG_STEPX
    sta HRG_X0

    lda HRG_ERR
    clc
    adc HRG_DY
    sta HRG_ERR
    jmp @y_major_loop

@line_done:
    rts

; Uses HRG_X0,HRG_Y0,HRG_X1,HRG_Y1,HRG_COLOR
HRG_DrawRect:
    cld
    jsr HRG_NormalizeX0X1
    jsr HRG_NormalizeY0Y1

    ; top
    lda HRG_MINX
    sta HRG_X0
    lda HRG_MAXX
    sta HRG_X1
    lda HRG_MINY
    sta HRG_Y0
    jsr HRG_DrawHLine

    ; bottom
    lda HRG_MINX
    sta HRG_X0
    lda HRG_MAXX
    sta HRG_X1
    lda HRG_MAXY
    sta HRG_Y0
    jsr HRG_DrawHLine

    ; left
    lda HRG_MINX
    sta HRG_X0
    lda HRG_MINY
    sta HRG_Y0
    lda HRG_MAXY
    sta HRG_Y1
    jsr HRG_DrawVLine

    ; right
    lda HRG_MAXX
    sta HRG_X0
    lda HRG_MINY
    sta HRG_Y0
    lda HRG_MAXY
    sta HRG_Y1
    jsr HRG_DrawVLine
    rts

; Uses HRG_X0,HRG_Y0,HRG_X1,HRG_Y1,HRG_COLOR
HRG_FillRect:
    cld
    jsr HRG_NormalizeX0X1
    jsr HRG_NormalizeY0Y1

    lda HRG_MINY
    sta HRG_CURY
@fill_rect_row:
    lda HRG_MINX
    sta HRG_X0
    lda HRG_MAXX
    sta HRG_X1
    lda HRG_CURY
    sta HRG_Y0
    jsr HRG_DrawHLine
    lda HRG_CURY
    cmp HRG_MAXY
    beq @fill_rect_done
    inc HRG_CURY
    jmp @fill_rect_row
@fill_rect_done:
    rts

; Uses HRG_X0,HRG_Y0,HRG_X1,HRG_Y1,HRG_X2,HRG_Y2,HRG_COLOR
HRG_DrawTriangle:
    cld
    ; Save original vertices.
    lda HRG_X0
    sta HRG_MINX
    lda HRG_Y0
    sta HRG_MINY
    lda HRG_X1
    sta HRG_MAXX
    lda HRG_Y1
    sta HRG_MAXY
    lda HRG_X2
    sta HRG_SAVE_X
    lda HRG_Y2
    sta HRG_SAVE_Y

    ; Edge 0: V0 -> V1
    lda HRG_MINX
    sta HRG_X0
    lda HRG_MINY
    sta HRG_Y0
    lda HRG_MAXX
    sta HRG_X1
    lda HRG_MAXY
    sta HRG_Y1
    jsr HRG_DrawLine

    ; Edge 1: V1 -> V2
    lda HRG_MAXX
    sta HRG_X0
    lda HRG_MAXY
    sta HRG_Y0
    lda HRG_SAVE_X
    sta HRG_X1
    lda HRG_SAVE_Y
    sta HRG_Y1
    jsr HRG_DrawLine

    ; Edge 2: V2 -> V0
    lda HRG_SAVE_X
    sta HRG_X0
    lda HRG_SAVE_Y
    sta HRG_Y0
    lda HRG_MINX
    sta HRG_X1
    lda HRG_MINY
    sta HRG_Y1
    jsr HRG_DrawLine

    ; Restore original vertex registers.
    lda HRG_MINX
    sta HRG_X0
    lda HRG_MINY
    sta HRG_Y0
    lda HRG_MAXX
    sta HRG_X1
    lda HRG_MAXY
    sta HRG_Y1
    lda HRG_SAVE_X
    sta HRG_X2
    lda HRG_SAVE_Y
    sta HRG_Y2
    rts

; Uses HRG_X0,HRG_Y0,HRG_X1,HRG_Y1,HRG_X2,HRG_Y2,HRG_COLOR
; Scanline fill with two incremental edge walkers.
HRG_FillTriangle:
    cld
    ; Preserve original vertex0 for final outline helper use.
    lda HRG_X0
    sta HRG_MINX
    lda HRG_Y0
    sta HRG_MINY

    jsr HRG_SortVerticesByY

    ; Degenerate horizontal triangle: draw one span.
    lda HRG_Y0
    cmp HRG_Y2
    bne @tri_not_flat
    ; Find min/max X across all 3 vertices.
    lda HRG_X0
    sta HRG_MINX
    sta HRG_MAXX

    lda HRG_X1
    cmp HRG_MINX
    bcs @tri_flat_min1_ok
    sta HRG_MINX
@tri_flat_min1_ok:
    cmp HRG_MAXX
    bcc @tri_flat_max1_ok
    sta HRG_MAXX
@tri_flat_max1_ok:

    lda HRG_X2
    cmp HRG_MINX
    bcs @tri_flat_min2_ok
    sta HRG_MINX
@tri_flat_min2_ok:
    cmp HRG_MAXX
    bcc @tri_flat_max2_ok
    sta HRG_MAXX
@tri_flat_max2_ok:

    lda HRG_MINX
    sta HRG_X0
    lda HRG_MAXX
    sta HRG_X1
    jsr HRG_DrawHLine
    rts
@tri_not_flat:

    ; Long edge: V0 -> V2 (E0)
    jsr HRG_InitEdge0_V0_V2

    ; Upper short edge: V0 -> V1 (E1)
    jsr HRG_InitEdge1_V0_V1

    lda HRG_Y0
    sta HRG_CURY

    ; Upper segment: y0 .. y1-1
    lda HRG_Y1
    sec
    sbc HRG_Y0
    sta HRG_COUNT
    beq @tri_lower_setup

@tri_upper_loop:
    jsr HRG_DrawSpanCurrentY
    jsr HRG_StepEdge0
    jsr HRG_StepEdge1
    inc HRG_CURY
    dec HRG_COUNT
    bne @tri_upper_loop

@tri_lower_setup:
    ; Lower short edge: V1 -> V2 (E1)
    jsr HRG_InitEdge1_V1_V2

    ; Lower segment: y1 .. y2 (inclusive)
    lda HRG_Y2
    sec
    sbc HRG_Y1
    sta HRG_COUNT

@tri_lower_loop:
    jsr HRG_DrawSpanCurrentY
    lda HRG_COUNT
    beq @tri_done
    jsr HRG_StepEdge0
    jsr HRG_StepEdge1
    inc HRG_CURY
    dec HRG_COUNT
    jmp @tri_lower_loop

@tri_done:
    rts

; Uses HRG_X0,HRG_Y0,HRG_X1,HRG_Y1,HRG_COLOR
; Midpoint-style integer ellipse outline.
HRG_DrawEllipse:
    cld
    jsr HRG_EllipseSetup
    bcs @draw_ellipse_done

    ; Outline path suppresses apex pixels to avoid spikes, so extend
    ; radius by 1 to preserve expected visual height.
    inc HRG_RY

    lda #$00
    sta HRG_INDEX
    jsr HRG_EllipseRasterCore

@draw_ellipse_done:
    rts

; Uses HRG_X0,HRG_Y0,HRG_X1,HRG_Y1,HRG_COLOR
; Midpoint-style integer filled ellipse.
HRG_FillEllipse:
    cld
    jsr HRG_EllipseSetup
    bcs @fill_ellipse_done

    lda #$01
    sta HRG_INDEX
    jsr HRG_EllipseRasterCore

@fill_ellipse_done:
    rts

; Shared ellipse raster core.
; Mode: HRG_INDEX = 0 (outline), non-zero (filled).
HRG_EllipseRasterCore:
    ; rx^2
    lda HRG_RX
    sta HRG_MUL_A
    sta HRG_MUL_B
    jsr HRG_Mul8x8
    lda HRG_MUL_LO
    sta HRG_MINX
    sta HRG_SAVE_X
    lda HRG_MUL_HI
    sta HRG_MAXX
    sta HRG_SAVE_Y

    ; ry^2 -> HRG_MUL_A/B (D2:D3)
    lda HRG_RY
    sta HRG_MUL_A
    sta HRG_MUL_B
    jsr HRG_Mul8x8
    lda HRG_MUL_LO
    sta HRG_MUL_A
    lda HRG_MUL_HI
    sta HRG_MUL_B

    ; 2*rx^2 -> HRG_MINX/HRG_MAXX
    lda HRG_SAVE_X
    asl A
    sta HRG_MINX
    lda HRG_SAVE_Y
    rol A
    sta HRG_MAXX

    ; 2*ry^2 -> HRG_MINY/HRG_MAXY
    lda HRG_MUL_A
    asl A
    sta HRG_MINY
    lda HRG_MUL_B
    rol A
    sta HRG_MAXY

    ; x = rx, y = 0
    lda HRG_RX
    sta HRG_PX2
    lda #$00
    sta HRG_PY2

    ; err24 = 0 (BF:C0:C1)
    sta HRG_E0_ERRL
    sta HRG_E0_ERRH
    sta HRG_E0_SX

    ; dy24 = rx^2 (C5:C6:C7)
    lda HRG_SAVE_X
    sta HRG_E1_ERRL
    lda HRG_SAVE_Y
    sta HRG_E1_ERRH
    lda #$00
    sta HRG_E1_SX

    ; dx24 = ry^2 * (2*rx-1) -> (C2:C3:C4)
    lda HRG_RX
    asl A
    sec
    sbc #$01
    sta HRG_TMP0
    jsr HRG_Mul16x8_To24

@ellipse_row_loop:
    lda HRG_INDEX
    bne @ellipse_fill_row
    jsr HRG_EllipsePlotOutlineRow
    jmp @ellipse_after_plot

@ellipse_fill_row:
    jsr HRG_EllipsePlotFilledRow

@ellipse_after_plot:
    ; y++
    inc HRG_PY2
    lda HRG_PY2
    cmp HRG_RY
    bcc @ellipse_update_error
    beq @ellipse_update_error
    rts

@ellipse_update_error:
    ; err += dy
    clc
    lda HRG_E0_ERRL
    adc HRG_E1_ERRL
    sta HRG_E0_ERRL
    lda HRG_E0_ERRH
    adc HRG_E1_ERRH
    sta HRG_E0_ERRH
    lda HRG_E0_SX
    adc HRG_E1_SX
    sta HRG_E0_SX

    ; dy += 2*rx^2
    clc
    lda HRG_E1_ERRL
    adc HRG_MINX
    sta HRG_E1_ERRL
    lda HRG_E1_ERRH
    adc HRG_MAXX
    sta HRG_E1_ERRH
    lda HRG_E1_SX
    adc #$00
    sta HRG_E1_SX

@ellipse_adjust_x:
    lda HRG_PX2
    beq @ellipse_row_loop

    ; while err >= dx
    lda HRG_E0_SX
    cmp HRG_E1_DY
    bcc @ellipse_row_loop
    bne @ellipse_do_sub
    lda HRG_E0_ERRH
    cmp HRG_E1_DX
    bcc @ellipse_row_loop
    bne @ellipse_do_sub
    lda HRG_E0_ERRL
    cmp HRG_E1_X
    bcc @ellipse_row_loop

@ellipse_do_sub:
    ; err -= dx
    sec
    lda HRG_E0_ERRL
    sbc HRG_E1_X
    sta HRG_E0_ERRL
    lda HRG_E0_ERRH
    sbc HRG_E1_DX
    sta HRG_E0_ERRH
    lda HRG_E0_SX
    sbc HRG_E1_DY
    sta HRG_E0_SX

    ; x--
    dec HRG_PX2

    ; dx -= 2*ry^2
    sec
    lda HRG_E1_X
    sbc HRG_MINY
    sta HRG_E1_X
    lda HRG_E1_DX
    sbc HRG_MAXY
    sta HRG_E1_DX
    lda HRG_E1_DY
    sbc #$00
    sta HRG_E1_DY
    jmp @ellipse_adjust_x

; Plot 4-way symmetric outline points using x=HRG_PX2, y=HRG_PY2.
HRG_EllipsePlotOutlineRow:
    ; Current left/right x in X0/X1.
    lda HRG_CX
    sec
    sbc HRG_PX2
    sta HRG_X0
    lda HRG_CX
    clc
    adc HRG_PX2
    sta HRG_X1

    lda HRG_CY
    sec
    sbc HRG_PY2
    sta HRG_Y0

    ; Suppress single apex pixels (x=0, y>0). We close the tip from
    ; the previous row instead to avoid detached center spikes.
    lda HRG_PX2
    bne @outline_plot_points
    lda HRG_PY2
    bne @outline_no_bottom

@outline_plot_points:
    ; Plot current top-left/top-right points.
    jsr HRG_SetPixelFromZP
    lda HRG_X1
    sta HRG_X0
    jsr HRG_SetPixelFromZP

    ; Plot current bottom pair when y != 0.
    lda HRG_PY2
    beq @outline_no_bottom

    lda HRG_CY
    clc
    adc HRG_PY2
    sta HRG_Y0

    lda HRG_CX
    sec
    sbc HRG_PX2
    sta HRG_X0
    jsr HRG_SetPixelFromZP
    lda HRG_CX
    clc
    adc HRG_PX2
    sta HRG_X0
    jsr HRG_SetPixelFromZP
@outline_no_bottom:

    ; First row has no previous row to connect.
    lda HRG_PY2
    bne @outline_chk_apex_now
    jmp @outline_store_prev

@outline_chk_apex_now:
    ; Connect until previous x also collapses to 0.
    lda HRG_FIRSTX
    bne @outline_connect_now
    jmp @outline_store_prev

@outline_connect_now:
    ; Apex row (x=0): close on previous row only (no apex center pixel).
    lda HRG_PX2
    bne @outline_connect_sides

    lda HRG_CX
    sec
    sbc HRG_FIRSTX
    sta HRG_X0
    lda HRG_CX
    clc
    adc HRG_FIRSTX
    sta HRG_X1
    lda HRG_CY
    sec
    sbc HRG_FIRSTY
    sta HRG_Y0
    jsr HRG_DrawHLine

    lda HRG_CY
    clc
    adc HRG_FIRSTY
    sta HRG_Y0
    jsr HRG_DrawHLine
    jmp @outline_store_prev

@outline_connect_sides:
    ; Connect top-left: (cx-prevx, cy-prevy) -> (cx-curx, cy-cury)
    lda HRG_CX
    sec
    sbc HRG_FIRSTX
    sta HRG_X0
    lda HRG_CY
    sec
    sbc HRG_FIRSTY
    sta HRG_Y0
    lda HRG_CX
    sec
    sbc HRG_PX2
    sta HRG_X1
    lda HRG_CY
    sec
    sbc HRG_PY2
    sta HRG_Y1
    jsr HRG_DrawLine

    ; Connect top-right: (cx+prevx, cy-prevy) -> (cx+curx, cy-cury)
    lda HRG_CX
    clc
    adc HRG_FIRSTX
    sta HRG_X0
    lda HRG_CY
    sec
    sbc HRG_FIRSTY
    sta HRG_Y0
    lda HRG_CX
    clc
    adc HRG_PX2
    sta HRG_X1
    lda HRG_CY
    sec
    sbc HRG_PY2
    sta HRG_Y1
    jsr HRG_DrawLine

    ; Connect bottom-left: (cx-prevx, cy+prevy) -> (cx-curx, cy+cury)
    lda HRG_CX
    sec
    sbc HRG_FIRSTX
    sta HRG_X0
    lda HRG_CY
    clc
    adc HRG_FIRSTY
    sta HRG_Y0
    lda HRG_CX
    sec
    sbc HRG_PX2
    sta HRG_X1
    lda HRG_CY
    clc
    adc HRG_PY2
    sta HRG_Y1
    jsr HRG_DrawLine

    ; Connect bottom-right: (cx+prevx, cy+prevy) -> (cx+curx, cy+cury)
    lda HRG_CX
    clc
    adc HRG_FIRSTX
    sta HRG_X0
    lda HRG_CY
    clc
    adc HRG_FIRSTY
    sta HRG_Y0
    lda HRG_CX
    clc
    adc HRG_PX2
    sta HRG_X1
    lda HRG_CY
    clc
    adc HRG_PY2
    sta HRG_Y1
    jsr HRG_DrawLine

@outline_store_prev:
    lda HRG_PX2
    sta HRG_FIRSTX
    lda HRG_PY2
    sta HRG_FIRSTY
    rts

; Plot 2-way symmetric horizontal fill spans using x=HRG_PX2, y=HRG_PY2.
HRG_EllipsePlotFilledRow:
    lda HRG_CX
    sec
    sbc HRG_PX2
    sta HRG_X0
    lda HRG_CX
    clc
    adc HRG_PX2
    sta HRG_X1

    lda HRG_CY
    sec
    sbc HRG_PY2
    sta HRG_Y0
    jsr HRG_DrawHLine

    lda HRG_PY2
    beq @filled_row_done

    lda HRG_CY
    clc
    adc HRG_PY2
    sta HRG_Y0
    jsr HRG_DrawHLine

@filled_row_done:
    rts

; 16x8 multiply for ellipse setup.
; Multiplicand: HRG_MUL_A:HRG_MUL_B (lo:hi)
; Multiplier  : HRG_TMP0
; Result      : HRG_E1_X:HRG_E1_DX:HRG_E1_DY (24-bit lo:mid:hi)
HRG_Mul16x8_To24:
    lda HRG_MUL_A
    sta HRG_TMP1
    lda HRG_MUL_B
    sta HRG_TMP2
    lda #$00
    sta HRG_DX

    lda #$00
    sta HRG_E1_X
    sta HRG_E1_DX
    sta HRG_E1_DY

    ldy #$08
@mul16x8_loop:
    lda HRG_TMP0
    and #$01
    beq @mul16x8_no_add

    clc
    lda HRG_E1_X
    adc HRG_TMP1
    sta HRG_E1_X
    lda HRG_E1_DX
    adc HRG_TMP2
    sta HRG_E1_DX
    lda HRG_E1_DY
    adc HRG_DX
    sta HRG_E1_DY

@mul16x8_no_add:
    ; Shift multiplicand 24-bit left.
    asl HRG_TMP1
    lda HRG_TMP2
    rol A
    sta HRG_TMP2
    lda HRG_DX
    rol A
    sta HRG_DX

    lsr HRG_TMP0
    dey
    bne @mul16x8_loop
    rts

; ------------------------------------------------------------
; Internal helpers
; ------------------------------------------------------------

HRG_FillAllHires:
    sta HRG_TMP0
    lda #(HIRES_BASE & $FF)
    sta HRG_PTRLO
    lda #((HIRES_BASE >> 8) & $FF)
    sta HRG_PTRHI
    ldx #$20
@fill_page:
    ldy #$00
    lda HRG_TMP0
@fill_byte:
    sta (HRG_PTRLO), Y
    iny
    bne @fill_byte
    inc HRG_PTRHI
    dex
    bne @fill_page
    rts

HRG_NormalizeX0X1:
    lda HRG_X0
    cmp HRG_X1
    bcc @x_ok
    beq @x_ok
    lda HRG_X0
    sta HRG_TMP0
    lda HRG_X1
    sta HRG_X0
    lda HRG_TMP0
    sta HRG_X1
@x_ok:
    lda HRG_X0
    sta HRG_MINX
    lda HRG_X1
    sta HRG_MAXX
    rts

HRG_NormalizeY0Y1:
    lda HRG_Y0
    cmp HRG_Y1
    bcc @y_ok
    beq @y_ok
    lda HRG_Y0
    sta HRG_TMP0
    lda HRG_Y1
    sta HRG_Y0
    lda HRG_TMP0
    sta HRG_Y1
@y_ok:
    lda HRG_Y0
    sta HRG_MINY
    lda HRG_Y1
    sta HRG_MAXY
    rts

; Set pixel at (HRG_X0,HRG_Y0) with HRG_COLOR.
HRG_SetPixelFromZP:
    jsr HRG_PreparePixelPointer
    jmp HRG_WritePreparedPixel

; Computes pointer/mask for pixel (HRG_X0,HRG_Y0).
HRG_PreparePixelPointer:
    lda HRG_X0
    and #$07
    tax
    lda HRG_BitMaskTable, X
    sta HRG_MASK
    lda HRG_InvMaskTable, X
    sta HRG_INVMASK

    lda HRG_X0
    lsr A
    lsr A
    lsr A
    sta HRG_TMP0

    lda HRG_Y0
    lsr A
    lsr A
    lsr A
    clc
    adc #((HIRES_BASE >> 8) & $FF)
    sta HRG_PTRHI

    lda HRG_Y0
    and #$07
    asl A
    asl A
    asl A
    asl A
    asl A
    clc
    adc HRG_TMP0
    sta HRG_PTRLO
    rts

; Writes all 4 RGBI planes for prepared pointer/masks using HRG_COLOR.
HRG_WritePreparedPixel:
    lda HRG_COLOR
    sta HRG_TMP2
    ldy #$00

    ; Red
    lda #HIRES_BANK_RED
    sta HIRES_BANK_SELECT
    lda (HRG_PTRLO), Y
    and HRG_INVMASK
    sta HRG_TMP1
    lsr HRG_TMP2
    bcc @wr_red_clear
    lda HRG_TMP1
    ora HRG_MASK
    sta (HRG_PTRLO), Y
    jmp @wr_green
@wr_red_clear:
    lda HRG_TMP1
    sta (HRG_PTRLO), Y

@wr_green:
    lda #HIRES_BANK_GREEN
    sta HIRES_BANK_SELECT
    lda (HRG_PTRLO), Y
    and HRG_INVMASK
    sta HRG_TMP1
    lsr HRG_TMP2
    bcc @wr_green_clear
    lda HRG_TMP1
    ora HRG_MASK
    sta (HRG_PTRLO), Y
    jmp @wr_blue
@wr_green_clear:
    lda HRG_TMP1
    sta (HRG_PTRLO), Y

@wr_blue:
    lda #HIRES_BANK_BLUE
    sta HIRES_BANK_SELECT
    lda (HRG_PTRLO), Y
    and HRG_INVMASK
    sta HRG_TMP1
    lsr HRG_TMP2
    bcc @wr_blue_clear
    lda HRG_TMP1
    ora HRG_MASK
    sta (HRG_PTRLO), Y
    jmp @wr_i
@wr_blue_clear:
    lda HRG_TMP1
    sta (HRG_PTRLO), Y

@wr_i:
    lda #HIRES_BANK_INTENSITY
    sta HIRES_BANK_SELECT
    lda (HRG_PTRLO), Y
    and HRG_INVMASK
    sta HRG_TMP1
    lsr HRG_TMP2
    bcc @wr_i_clear
    lda HRG_TMP1
    ora HRG_MASK
    sta (HRG_PTRLO), Y
    rts
@wr_i_clear:
    lda HRG_TMP1
    sta (HRG_PTRLO), Y
    rts

; Horizontal line with byte-span fast path.
; Uses HRG_X0,HRG_X1,HRG_Y0,HRG_COLOR
HRG_DrawHLine:
    ; Preserve caller endpoints.
    lda HRG_X0
    sta HRG_SAVE_X
    lda HRG_X1
    sta HRG_SAVE_Y

    ; Build sorted local range into CURX..LINE_END.
    lda HRG_X0
    cmp HRG_X1
    bcc @hl_x0_first
    beq @hl_equal
    lda HRG_X1
    sta HRG_CURX
    lda HRG_X0
    sta HRG_LINE_END
    jmp @hl_sorted

@hl_x0_first:
    lda HRG_X0
    sta HRG_CURX
    lda HRG_X1
    sta HRG_LINE_END
    jmp @hl_sorted

@hl_equal:
    ; Single pixel keeps old path to avoid overhead.
    lda HRG_X0
    sta HRG_CURX
    sta HRG_LINE_END
    jsr HRG_SetPixelFromZP
    jmp @hl_done

@hl_sorted:
    ; Byte offsets for start/end.
    lda HRG_CURX
    lsr A
    lsr A
    lsr A
    sta HRG_TMP0      ; start byte (0..31)

    lda HRG_LINE_END
    lsr A
    lsr A
    lsr A
    sta HRG_TMP1      ; end byte (0..31)

    ; Masks for first/last bytes.
    lda HRG_CURX
    and #$07
    tax
    lda HRG_HLineStartMaskTable, X
    sta HRG_MASK

    lda HRG_LINE_END
    and #$07
    tax
    lda HRG_HLineEndMaskTable, X
    sta HRG_INVMASK

    ; Byte delta = end - start.
    lda HRG_TMP1
    sec
    sbc HRG_TMP0
    sta HRG_TMP2

    ; Row base pointer at start byte.
    lda HRG_Y0
    lsr A
    lsr A
    lsr A
    clc
    adc #((HIRES_BASE >> 8) & $FF)
    sta HRG_PTRHI

    lda HRG_Y0
    and #$07
    asl A
    asl A
    asl A
    asl A
    asl A
    clc
    adc HRG_TMP0
    sta HRG_PTRLO

    ; Red plane
    lda #HIRES_BANK_RED
    sta HIRES_BANK_SELECT
    lda HRG_COLOR
    and #$01
    beq @hl_red_clear
    lda #$FF
    jsr HRG_DrawHLinePlane
    jmp @hl_green
@hl_red_clear:
    lda #$00
    jsr HRG_DrawHLinePlane

@hl_green:
    lda #HIRES_BANK_GREEN
    sta HIRES_BANK_SELECT
    lda HRG_COLOR
    and #$02
    beq @hl_green_clear
    lda #$FF
    jsr HRG_DrawHLinePlane
    jmp @hl_blue
@hl_green_clear:
    lda #$00
    jsr HRG_DrawHLinePlane

@hl_blue:
    lda #HIRES_BANK_BLUE
    sta HIRES_BANK_SELECT
    lda HRG_COLOR
    and #$04
    beq @hl_blue_clear
    lda #$FF
    jsr HRG_DrawHLinePlane
    jmp @hl_i
@hl_blue_clear:
    lda #$00
    jsr HRG_DrawHLinePlane

@hl_i:
    lda #HIRES_BANK_INTENSITY
    sta HIRES_BANK_SELECT
    lda HRG_COLOR
    and #$08
    beq @hl_i_clear
    lda #$FF
    jsr HRG_DrawHLinePlane
    jmp @hl_done
@hl_i_clear:
    lda #$00
    jsr HRG_DrawHLinePlane

@hl_done:
    lda HRG_SAVE_X
    sta HRG_X0
    lda HRG_SAVE_Y
    sta HRG_X1
    rts

; Draw one horizontal byte span in current selected plane.
; In: A = $FF (set) or $00 (clear)
; Uses: HRG_PTRLO/HI base pointer, HRG_MASK start mask, HRG_INVMASK end mask,
;       HRG_TMP2 byte delta (end-start).
HRG_DrawHLinePlane:
    sta HRG_DX
    ldy #$00

    lda HRG_TMP2
    bne @hlp_multi

    ; Single-byte span.
    lda HRG_MASK
    and HRG_INVMASK
    sta HRG_DY
    lda HRG_DX
    beq @hlp_single_clear
    lda (HRG_PTRLO), Y
    ora HRG_DY
    sta (HRG_PTRLO), Y
    rts
@hlp_single_clear:
    lda HRG_DY
    eor #$FF
    sta HRG_DY
    lda (HRG_PTRLO), Y
    and HRG_DY
    sta (HRG_PTRLO), Y
    rts

@hlp_multi:
    ; First byte (partial).
    lda HRG_DX
    beq @hlp_first_clear
    lda (HRG_PTRLO), Y
    ora HRG_MASK
    sta (HRG_PTRLO), Y
    jmp @hlp_mid
@hlp_first_clear:
    lda HRG_MASK
    eor #$FF
    sta HRG_DY
    lda (HRG_PTRLO), Y
    and HRG_DY
    sta (HRG_PTRLO), Y

@hlp_mid:
    ; Middle full bytes.
    lda HRG_TMP2
    sec
    sbc #$01
    tax
    beq @hlp_end
@hlp_mid_loop:
    iny
    lda HRG_DX
    sta (HRG_PTRLO), Y
    dex
    bne @hlp_mid_loop

@hlp_end:
    ; Last byte (partial).
    iny
    lda HRG_DX
    beq @hlp_end_clear
    lda (HRG_PTRLO), Y
    ora HRG_INVMASK
    sta (HRG_PTRLO), Y
    rts
@hlp_end_clear:
    lda HRG_INVMASK
    eor #$FF
    sta HRG_DY
    lda (HRG_PTRLO), Y
    and HRG_DY
    sta (HRG_PTRLO), Y
    rts

; Vertical line using repeated pixel writes.
; Uses HRG_X0,HRG_Y0,HRG_Y1,HRG_COLOR
HRG_DrawVLine:
    ; Preserve caller endpoints.
    lda HRG_Y0
    sta HRG_SAVE_X
    lda HRG_Y1
    sta HRG_SAVE_Y

    ; Build sorted local range into CURY..LINE_END.
    lda HRG_Y0
    cmp HRG_Y1
    bcc @vl_y0_first
    beq @vl_equal
    lda HRG_Y1
    sta HRG_CURY
    lda HRG_Y0
    sta HRG_LINE_END
    jmp @vl_loop

@vl_y0_first:
    lda HRG_Y0
    sta HRG_CURY
    lda HRG_Y1
    sta HRG_LINE_END
    jmp @vl_loop

@vl_equal:
    lda HRG_Y0
    sta HRG_CURY
    sta HRG_LINE_END

@vl_loop:
    lda HRG_CURY
    sta HRG_Y0
    jsr HRG_SetPixelFromZP
    lda HRG_CURY
    cmp HRG_LINE_END
    beq @vl_done
    inc HRG_CURY
    jmp @vl_loop
@vl_done:
    lda HRG_SAVE_X
    sta HRG_Y0
    lda HRG_SAVE_Y
    sta HRG_Y1
    rts

; Bubble-sort 3 vertices by Y ascending (keeps X paired).
HRG_SortVerticesByY:
    lda HRG_Y0
    cmp HRG_Y1
    bcc @svy_01_ok
    beq @svy_01_ok
    jsr HRG_Swap01
@svy_01_ok:
    lda HRG_Y1
    cmp HRG_Y2
    bcc @svy_12_ok
    beq @svy_12_ok
    jsr HRG_Swap12
@svy_12_ok:
    lda HRG_Y0
    cmp HRG_Y1
    bcc @svy_done
    beq @svy_done
    jsr HRG_Swap01
@svy_done:
    rts

HRG_Swap01:
    lda HRG_X0
    sta HRG_TMP0
    lda HRG_X1
    sta HRG_X0
    lda HRG_TMP0
    sta HRG_X1

    lda HRG_Y0
    sta HRG_TMP0
    lda HRG_Y1
    sta HRG_Y0
    lda HRG_TMP0
    sta HRG_Y1
    rts

HRG_Swap12:
    lda HRG_X1
    sta HRG_TMP0
    lda HRG_X2
    sta HRG_X1
    lda HRG_TMP0
    sta HRG_X2

    lda HRG_Y1
    sta HRG_TMP0
    lda HRG_Y2
    sta HRG_Y1
    lda HRG_TMP0
    sta HRG_Y2
    rts

HRG_InitEdge0_V0_V2:
    lda HRG_X0
    sta HRG_E0_X

    lda HRG_X2
    sec
    sbc HRG_X0
    bcs @e0_dx_pos
    eor #$FF
    clc
    adc #$01
    sta HRG_E0_DX
    lda #$FF
    sta HRG_E0_SX
    jmp @e0_dy
@e0_dx_pos:
    sta HRG_E0_DX
    lda #$01
    sta HRG_E0_SX

@e0_dy:
    lda HRG_Y2
    sec
    sbc HRG_Y0
    sta HRG_E0_DY
    lda #$00
    sta HRG_E0_ERRL
    sta HRG_E0_ERRH
    rts

HRG_InitEdge1_V0_V1:
    lda HRG_X0
    sta HRG_E1_X

    lda HRG_X1
    sec
    sbc HRG_X0
    bcs @e1a_dx_pos
    eor #$FF
    clc
    adc #$01
    sta HRG_E1_DX
    lda #$FF
    sta HRG_E1_SX
    jmp @e1a_dy
@e1a_dx_pos:
    sta HRG_E1_DX
    lda #$01
    sta HRG_E1_SX

@e1a_dy:
    lda HRG_Y1
    sec
    sbc HRG_Y0
    sta HRG_E1_DY
    lda #$00
    sta HRG_E1_ERRL
    sta HRG_E1_ERRH
    rts

HRG_InitEdge1_V1_V2:
    lda HRG_X1
    sta HRG_E1_X

    lda HRG_X2
    sec
    sbc HRG_X1
    bcs @e1b_dx_pos
    eor #$FF
    clc
    adc #$01
    sta HRG_E1_DX
    lda #$FF
    sta HRG_E1_SX
    jmp @e1b_dy
@e1b_dx_pos:
    sta HRG_E1_DX
    lda #$01
    sta HRG_E1_SX

@e1b_dy:
    lda HRG_Y2
    sec
    sbc HRG_Y1
    sta HRG_E1_DY
    lda #$00
    sta HRG_E1_ERRL
    sta HRG_E1_ERRH
    rts

HRG_StepEdge0:
    lda HRG_E0_DY
    bne @e0_step_go
    rts
@e0_step_go:
    clc
    lda HRG_E0_ERRL
    adc HRG_E0_DX
    sta HRG_E0_ERRL
    lda HRG_E0_ERRH
    adc #$00
    sta HRG_E0_ERRH

@e0_adj_check:
    lda HRG_E0_ERRH
    bne @e0_adjust
    lda HRG_E0_ERRL
    cmp HRG_E0_DY
    bcc @e0_done
@e0_adjust:
    sec
    lda HRG_E0_ERRL
    sbc HRG_E0_DY
    sta HRG_E0_ERRL
    lda HRG_E0_ERRH
    sbc #$00
    sta HRG_E0_ERRH

    lda HRG_E0_X
    clc
    adc HRG_E0_SX
    sta HRG_E0_X
    jmp @e0_adj_check
@e0_done:
    rts

HRG_StepEdge1:
    lda HRG_E1_DY
    bne @e1_step_go
    rts
@e1_step_go:
    clc
    lda HRG_E1_ERRL
    adc HRG_E1_DX
    sta HRG_E1_ERRL
    lda HRG_E1_ERRH
    adc #$00
    sta HRG_E1_ERRH

@e1_adj_check:
    lda HRG_E1_ERRH
    bne @e1_adjust
    lda HRG_E1_ERRL
    cmp HRG_E1_DY
    bcc @e1_done
@e1_adjust:
    sec
    lda HRG_E1_ERRL
    sbc HRG_E1_DY
    sta HRG_E1_ERRL
    lda HRG_E1_ERRH
    sbc #$00
    sta HRG_E1_ERRH

    lda HRG_E1_X
    clc
    adc HRG_E1_SX
    sta HRG_E1_X
    jmp @e1_adj_check
@e1_done:
    rts

HRG_DrawSpanCurrentY:
    lda HRG_E0_X
    sta HRG_X0
    lda HRG_E1_X
    sta HRG_X1
    lda HRG_CURY
    sta HRG_Y0
    jmp HRG_DrawHLine

; Prepare center/radii from HRG_X0..HRG_Y1.
; Returns C=1 when degenerate.
HRG_EllipseSetup:
    jsr HRG_NormalizeX0X1
    jsr HRG_NormalizeY0Y1

    ; cx = minx + ((maxx - minx) >> 1)
    lda HRG_MAXX
    sec
    sbc HRG_MINX
    lsr A
    clc
    adc HRG_MINX
    sta HRG_CX

    ; cy = miny + ((maxy - miny) >> 1)
    lda HRG_MAXY
    sec
    sbc HRG_MINY
    lsr A
    clc
    adc HRG_MINY
    sta HRG_CY

    lda HRG_MAXX
    sec
    sbc HRG_MINX
    lsr A
    sta HRG_RX
    beq @ellipse_deg

    lda HRG_MAXY
    sec
    sbc HRG_MINY
    lsr A
    sta HRG_RY
    beq @ellipse_deg

    clc
    rts

@ellipse_deg:
    sec
    rts

; X=index (0..63)
; Out: HRG_PX, HRG_PY
HRG_EllipsePointFromIndex:
    stx HRG_SAVE_X

    lda HRG_Cos64, X
    sta HRG_MUL_B
    lda HRG_RX
    jsr HRG_ScaleSignedQ7
    clc
    adc HRG_CX
    sta HRG_PX

    ldx HRG_SAVE_X
    lda HRG_Sin64, X
    sta HRG_MUL_B
    lda HRG_RY
    jsr HRG_ScaleSignedQ7
    clc
    adc HRG_CY
    sta HRG_PY

    ldx HRG_SAVE_X
    rts

; A = unsigned radius (0..127)
; HRG_MUL_B = signed factor (-127..127) in Q0.7
; returns A = signed scaled delta ((radius*factor)>>7)
HRG_ScaleSignedQ7:
    sta HRG_MUL_A
    lda #$00
    sta HRG_SIGN

    lda HRG_MUL_B
    bpl @scale_abs_done
    lda #$01
    sta HRG_SIGN
    lda HRG_MUL_B
    eor #$FF
    clc
    adc #$01
    sta HRG_MUL_B
@scale_abs_done:

    jsr HRG_Mul8x8

    ; product >> 7
    lda HRG_MUL_HI
    asl A
    sta HRG_TMP0
    lda HRG_MUL_LO
    lsr A
    lsr A
    lsr A
    lsr A
    lsr A
    lsr A
    lsr A
    ora HRG_TMP0

    ldx HRG_SIGN
    beq @scale_done
    eor #$FF
    clc
    adc #$01
@scale_done:
    rts

; Unsigned 8x8 multiply: HRG_MUL_A * HRG_MUL_B -> HRG_MUL_HI:HRG_MUL_LO
HRG_Mul8x8:
    ; 16-bit multiplicand in TMP2:TMP0
    lda HRG_MUL_A
    sta HRG_TMP0
    lda #$00
    sta HRG_TMP2

    lda HRG_MUL_B
    sta HRG_TMP1

    lda #$00
    sta HRG_MUL_LO
    sta HRG_MUL_HI
    ldx #$08

@mul2_loop:
    lda HRG_TMP1
    and #$01
    beq @mul2_skip
    clc
    lda HRG_MUL_LO
    adc HRG_TMP0
    sta HRG_MUL_LO
    lda HRG_MUL_HI
    adc HRG_TMP2
    sta HRG_MUL_HI
@mul2_skip:
    asl HRG_TMP0
    bcc @mul2_no_carry
    inc HRG_TMP2
@mul2_no_carry:
    lsr HRG_TMP1
    dex
    bne @mul2_loop
    rts

HRG_BitMaskTable:
    .byte $80, $40, $20, $10, $08, $04, $02, $01

HRG_InvMaskTable:
    .byte $7F, $BF, $DF, $EF, $F7, $FB, $FD, $FE

; Byte masks for horizontal line endpoints.
HRG_HLineStartMaskTable:
    .byte $FF, $7F, $3F, $1F, $0F, $07, $03, $01
HRG_HLineEndMaskTable:
    .byte $80, $C0, $E0, $F0, $F8, $FC, $FE, $FF

; 64-step unit circle in Q0.7 signed format.
HRG_Cos64:
    .byte $7F,$7E,$7D,$7A,$75,$70,$6A,$62,$5A,$51,$47,$3C,$31,$25,$19,$0C
    .byte $00,$F4,$E7,$DB,$CF,$C4,$B9,$AF,$A6,$9E,$96,$90,$8B,$86,$83,$82
    .byte $81,$82,$83,$86,$8B,$90,$96,$9E,$A6,$AF,$B9,$C4,$CF,$DB,$E7,$F4
    .byte $00,$0C,$19,$25,$31,$3C,$47,$51,$5A,$62,$6A,$70,$75,$7A,$7D,$7E

HRG_Sin64:
    .byte $00,$0C,$19,$25,$31,$3C,$47,$51,$5A,$62,$6A,$70,$75,$7A,$7D,$7E
    .byte $7F,$7E,$7D,$7A,$75,$70,$6A,$62,$5A,$51,$47,$3C,$31,$25,$19,$0C
    .byte $00,$F4,$E7,$DB,$CF,$C4,$B9,$AF,$A6,$9E,$96,$90,$8B,$86,$83,$82
    .byte $81,$82,$83,$86,$8B,$90,$96,$9E,$A6,$AF,$B9,$C4,$CF,$DB,$E7,$F4
