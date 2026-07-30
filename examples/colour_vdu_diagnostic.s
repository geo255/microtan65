; colour_vdu_diagnostic.s
; Mousepacket Colour VDU and MC6845 diagnostic for microtan65.
;
; Enable the Colour VDU card, load this file, run at $0400, then select
; "Mousepacket Colour VDU output" from the emulator display menu.
;
; The CRTC display start is deliberately set to $0040, so visible row zero
; comes from CPU address $A040 rather than $A000.

.org $0400
    jmp Start

.include "includes/microtan_hw_labels.inc"

.equ SOURCE_PTR, $0040
.equ DEST_PTR,   $0042
.equ LINE_COUNT, 21

; SAA5050 control codes.
.equ TT_ALPHA_RED,       $01
.equ TT_ALPHA_GREEN,     $02
.equ TT_ALPHA_YELLOW,    $03
.equ TT_ALPHA_BLUE,      $04
.equ TT_ALPHA_MAGENTA,   $05
.equ TT_ALPHA_CYAN,      $06
.equ TT_ALPHA_WHITE,     $07
.equ TT_FLASH,           $08
.equ TT_STEADY,          $09
.equ TT_NORMAL_HEIGHT,   $0C
.equ TT_DOUBLE_HEIGHT,   $0D
.equ TT_GRAPHICS_RED,    $11
.equ TT_GRAPHICS_GREEN,  $12
.equ TT_GRAPHICS_YELLOW, $13
.equ TT_GRAPHICS_CYAN,   $16
.equ TT_CONCEAL,         $18
.equ TT_CONTIGUOUS,      $19
.equ TT_SEPARATED,       $1A
.equ TT_BLACK_BG,        $1C
.equ TT_NEW_BG,          $1D
.equ TT_HOLD_GRAPHICS,   $1E
.equ TT_RELEASE_GRAPHICS,$1F

Start:
    ; Clear only the 1600 visible cells.  Leave firmware workspace at
    ; $A640-$A7EF, including TANBUG's output-change flag at $A642, alone.
    lda #' '
    ldx #$00
@clear:
    sta $A000,x
    sta $A100,x
    sta $A200,x
    sta $A300,x
    sta $A400,x
    sta $A500,x
    inx
    bne @clear
    ldx #$3F
@clear_last_row:
    sta $A600,x
    dex
    bpl @clear_last_row

    ; Use 24 rows because the $0040 start offset makes row 25 overlap
    ; TANBUG workspace at $A640-$A67F.
    lda #CRTC_H_DISPLAYED
    ldx #64
    jsr WriteCRTC
    lda #CRTC_V_DISPLAYED
    ldx #24
    jsr WriteCRTC
    lda #CRTC_SCANLINES
    ldx #9
    jsr WriteCRTC

    ; Start at VDU RAM offset $0040 to exercise CRTC start-address handling.
    lda #CRTC_START_ADDRESS_HI
    ldx #$00
    jsr WriteCRTC
    lda #CRTC_START_ADDRESS_LO
    ldx #$40
    jsr WriteCRTC

    ; Blink a two-scanline cursor inside the brackets on diagnostic row 15.
    ; Address = $0040 + (15 * 64) + 26 = $041A.
    lda #CRTC_CURSOR_START
    ldx #$48
    jsr WriteCRTC
    lda #CRTC_CURSOR_END
    ldx #$09
    jsr WriteCRTC
    lda #CRTC_CURSOR_ADDRESS_HI
    ldx #$04
    jsr WriteCRTC
    lda #CRTC_CURSOR_ADDRESS_LO
    ldx #$1A
    jsr WriteCRTC

    ; Copy each zero-terminated diagnostic row to its CRTC-visible address.
    ldx #$00
@next_line:
    txa
    asl a
    tay
    lda LinePointers,y
    sta SOURCE_PTR
    lda LinePointers + 1,y
    sta SOURCE_PTR + 1
    lda RowAddressLow,x
    sta DEST_PTR
    lda RowAddressHigh,x
    sta DEST_PTR + 1
    jsr CopyString
    inx
    cpx #LINE_COUNT
    bne @next_line

    ; Keep the completed diagnostic visible. Reset the emulator to exit.
@finished:
    jmp @finished

WriteCRTC:
    sta COLOUR_VDU_CRTC_SELECT
    stx COLOUR_VDU_CRTC_DATA
    rts

CopyString:
    ldy #$00
@copy:
    lda (SOURCE_PTR),y
    beq @done
    sta (DEST_PTR),y
    iny
    bne @copy
@done:
    rts

LinePointers:
    .word Line00, Line01, Line02, Line03, Line04, Line05
    .word Line06, Line07, Line08, Line09, Line10, Line11
    .word Line12, Line13, Line14, Line15, Line16, Line17
    .word Line18, Line19, Line20

; Rows start at CPU address $A040 because CRTC start address is $0040.
RowAddressLow:
    .byte $40, $80, $C0, $00, $40, $80, $C0, $00, $40
    .byte $80, $C0, $00, $40, $80, $C0, $00, $40, $80
    .byte $C0, $00, $40
RowAddressHigh:
    .byte $A0, $A0, $A0, $A1, $A1, $A1, $A1, $A2, $A2
    .byte $A2, $A2, $A3, $A3, $A3, $A3, $A4, $A4, $A4
    .byte $A4, $A5, $A5

Line00:
    .byte TT_ALPHA_CYAN
    .text "MICROTAN 65 - MOUSEPACKET COLOUR VDU DIAGNOSTIC"
    .byte $00
Line01:
    .byte TT_ALPHA_WHITE
    .text "CRTC START $0040: THIS ROW IS STORED AT CPU $A040"
    .byte $00
Line02:
    .byte TT_ALPHA_YELLOW
    .text "ALPHANUMERIC COLOURS"
    .byte $00
Line03:
    .byte TT_ALPHA_RED
    .text " RED  "
    .byte TT_ALPHA_GREEN
    .text " GREEN  "
    .byte TT_ALPHA_YELLOW
    .text " YELLOW"
    .byte $00
Line04:
    .byte TT_ALPHA_BLUE
    .text " BLUE  "
    .byte TT_ALPHA_MAGENTA
    .text " MAGENTA  "
    .byte TT_ALPHA_CYAN
    .text " CYAN  "
    .byte TT_ALPHA_WHITE
    .text " WHITE"
    .byte $00
Line05:
    .byte TT_ALPHA_RED, TT_NEW_BG, TT_ALPHA_WHITE
    .text " RED BACKGROUND "
    .byte TT_BLACK_BG, TT_ALPHA_CYAN
    .text " BLACK BACKGROUND"
    .byte $00
Line06:
    .byte TT_ALPHA_WHITE
    .text "STEADY  "
    .byte TT_ALPHA_YELLOW, TT_FLASH
    .text "FLASHING TEXT"
    .byte TT_STEADY, TT_ALPHA_WHITE
    .text "  STEADY AGAIN"
    .byte $00
Line07:
    .byte TT_ALPHA_WHITE
    .text "VISIBLE  "
    .byte TT_CONCEAL
    .text "THIS IS CONCEALED"
    .byte TT_ALPHA_GREEN
    .text "  VISIBLE AGAIN"
    .byte $00
Line08:
    .byte TT_ALPHA_CYAN
    .text "NORMAL  "
    ; "INVERSE" with bit 7 set on each character.
    .byte $C9, $CE, $D6, $C5, $D2, $D3, $C5
    .text "  NORMAL"
    .byte $00
Line09:
    .byte TT_ALPHA_GREEN
    .text "CONTIGUOUS MOSAICS"
    .byte $00
Line10:
    .byte TT_GRAPHICS_GREEN, TT_CONTIGUOUS
    .byte $21, $22, $24, $28, $30, $60, $61, $63
    .byte $67, $6F, $7F, $6F, $67, $63, $61, $60
    .byte $30, $28, $24, $22, $21
    .byte $00
Line11:
    .byte TT_ALPHA_CYAN
    .text "SEPARATED MOSAICS"
    .byte $00
Line12:
    .byte TT_GRAPHICS_CYAN, TT_SEPARATED
    .byte $21, $22, $24, $28, $30, $60, $61, $63
    .byte $67, $6F, $7F, $6F, $67, $63, $61, $60
    .byte $30, $28, $24, $22, $21
    .byte $00
Line13:
    .byte TT_GRAPHICS_YELLOW, $7F, TT_HOLD_GRAPHICS
    .byte TT_GRAPHICS_RED, TT_GRAPHICS_GREEN, TT_GRAPHICS_CYAN
    .byte TT_RELEASE_GRAPHICS, TT_ALPHA_WHITE
    .text " HOLD / RELEASE GRAPHICS"
    .byte $00
Line14:
    .byte TT_ALPHA_YELLOW
    .text "MC6845 CURSOR AND ADDRESS TEST"
    .byte $00
Line15:
    .byte TT_ALPHA_WHITE
    .text "MC6845 BLINKING CURSOR: [ ]"
    .byte $00
Line16:
    .byte TT_ALPHA_MAGENTA
    .text "CURSOR SHOULD BLINK INSIDE THE BRACKETS ABOVE"
    .byte $00
Line17:
    .byte TT_ALPHA_YELLOW
    .text "DOUBLE HEIGHT AND NORMAL HEIGHT"
    .byte $00
Line18:
    .byte TT_ALPHA_CYAN
    .text "HEIGHT: "
    .byte TT_DOUBLE_HEIGHT
    .text "DOUBLE"
    .byte TT_NORMAL_HEIGHT, TT_ALPHA_WHITE
    .text " NORMAL"
    .byte $00
Line19:
    ; Kept blank so the lower half of DOUBLE is clearly visible.
    .byte $00
Line20:
    .byte TT_ALPHA_GREEN
    .text "END OF COLOUR VDU DIAGNOSTIC"
    .byte $00
