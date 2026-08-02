; rtc_time_display.s
; Read the ETI real-time clock and continuously display HH:MM:SS.
;
; Assemble with:
;   python3 tools/asm6502.py examples/rtc_time_display.s \
;       -o examples/rtc_time_display.hex
;
; Load the Intel HEX file in the emulator and run from $0400.

.org $0400

    jmp Start

.include "includes/microtan_hw_labels.inc"

.equ TIME_LINE,        $02E0       ; Row 7 of the 32-column text display
.equ TIME_TEXT_LENGTH, $11         ; Length of "RTC TIME 00:00:00"

Start:
    ; Make subsequent display writes normal text rather than graphics or
    ; inverse characters.
    lda #$00
    sta CHUNKY_DISABLE
    sta INVERSE_DISABLE

    ; Clear the 512-byte text display.
    ldx #$00
ClearScreen:
    lda #$20
    sta DISPLAY_RAM_START,x
    sta DISPLAY_RAM_START+$100,x
    inx
    bne ClearScreen

    ; Copy the fixed display text.  The six zeroes are replaced below.
    ldx #$00
CopyText:
    lda TimeText,x
    sta TIME_LINE,x
    inx
    cpx #TIME_TEXT_LENGTH
    bne CopyText

MainLoop:
    jsr ReadTime
    jsr DisplayTime

    ; Avoid rewriting the display until the RTC announces its next update.
WaitForUpdate:
    lda RTC_TENTHS
    cmp #$0F
    beq MainLoop
    cmp TimeTenths
    beq WaitForUpdate
    jmp MainLoop

; Take a coherent snapshot.  The RTC returns $0F on the first access after
; each tenth-second update, so restart if that marker appears anywhere in the
; sequence.  Comparing the first and final tenths digits also protects against
; an update at the edge of the read sequence.
ReadTime:
TryRead:
    lda RTC_TENTHS
    cmp #$0F
    beq TryRead
    sta TimeTenths

    lda RTC_HOURS_TENS
    cmp #$0F
    beq TryRead
    sta TimeHoursTens

    lda RTC_HOURS_UNITS
    cmp #$0F
    beq TryRead
    sta TimeHoursUnits

    lda RTC_MINUTES_TENS
    cmp #$0F
    beq TryRead
    sta TimeMinutesTens

    lda RTC_MINUTES_UNITS
    cmp #$0F
    beq TryRead
    sta TimeMinutesUnits

    lda RTC_SECONDS_TENS
    cmp #$0F
    beq TryRead
    sta TimeSecondsTens

    lda RTC_SECONDS_UNITS
    cmp #$0F
    beq TryRead
    sta TimeSecondsUnits

    lda RTC_TENTHS
    cmp #$0F
    beq TryRead
    cmp TimeTenths
    bne TryRead
    rts

; Convert each RTC digit to ASCII and place it over the zeroes in TimeText.
DisplayTime:
    lda TimeHoursTens
    ora #$30
    sta TIME_LINE+9
    lda TimeHoursUnits
    ora #$30
    sta TIME_LINE+10

    lda TimeMinutesTens
    ora #$30
    sta TIME_LINE+12
    lda TimeMinutesUnits
    ora #$30
    sta TIME_LINE+13

    lda TimeSecondsTens
    ora #$30
    sta TIME_LINE+15
    lda TimeSecondsUnits
    ora #$30
    sta TIME_LINE+16
    rts

TimeText:
    .text "RTC TIME 00:00:00"

TimeTenths:
    .byte $00
TimeHoursTens:
    .byte $00
TimeHoursUnits:
    .byte $00
TimeMinutesTens:
    .byte $00
TimeMinutesUnits:
    .byte $00
TimeSecondsTens:
    .byte $00
TimeSecondsUnits:
    .byte $00
