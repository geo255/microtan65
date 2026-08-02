; microtan_hw_labels.inc
; Memory map labels for AY8910 and 6522 devices in microtan65.

; ---------------------------------------------------------------------------
; TANBUG/XBUG zero-page usage
; ---------------------------------------------------------------------------
; Names from the Microtan manual.
.equ NULL,                   $0000
.equ ICHAR,                  $0001
.equ OCHAR,                  $0002
.equ VDUIND,                 $0003
.equ INTFS1,                 $0004
.equ INTFS2,                 $0005
.equ INTFS3,                 $0006
.equ NMIJP,                  $0007
.equ NMIJP1,                 $0008
.equ NMIJP2,                 $0009
.equ ICURS,                  $000A
.equ ICURSH,                 $000B
.equ RUNIND,                 $000C
.equ SINGLE,                 $000D
.equ PROCED,                 $000E
.equ SIMCOM,                 $000F
.equ INTSL1,                 $0010
.equ INTSL2,                 $0011
.equ INTSL3,                 $0012
.equ HXPKL,                  $0013
.equ HXPKH,                  $0014
.equ PCLBCK,                 $0015
.equ PCHBCK,                 $0016
.equ PSWBCK,                 $0017
.equ SPBCK,                  $0018
.equ XBCK,                   $0019
.equ YBCK,                   $001A
.equ ABCK,                   $001B
; Manual text appears as "$ooic MODADL": interpreted as $001C.
.equ MODADL,                 $001C
.equ MODADH,                 $001D
.equ COPL,                   $001E
.equ COPH,                   $001F
.equ BPTLO,                  $0020
.equ BPTHL,                  $0028
.equ BPTCOD,                 $0030

; Compatibility aliases.
.equ ZP_BREAKPOINT_OP,        NULL
.equ ZP_LAST_ASCII_KEY,       ICHAR
.equ ZP_CHAR_TEMP,            OCHAR
.equ ZP_DISPLAY_INDEX,        VDUIND
.equ ZP_FAST_IRQ_VEC_LO,      INTFS1
.equ ZP_FAST_IRQ_VEC_HI,      INTFS2
.equ ZP_FAST_IRQ_VEC_BANK,    INTFS3
.equ ZP_NMI_VEC_LO,           NMIJP
.equ ZP_NMI_VEC_HI,           NMIJP1
.equ ZP_NMI_VEC_BANK,         NMIJP2
.equ ZP_CURSOR_INDEX_LO,      ICURS
.equ ZP_CURSOR_INDEX_HI,      ICURSH
.equ ZP_IN_USER_PROGRAM,      RUNIND
.equ ZP_SINGLE_STEP_FLAG,     SINGLE
.equ ZP_PROCEED_COUNTER,      PROCED
.equ ZP_HEX_ASCII_MODE,       SIMCOM
.equ ZP_SLOW_IRQ_VEC_LO,      INTSL1
.equ ZP_SLOW_IRQ_VEC_HI,      INTSL2
.equ ZP_SLOW_IRQ_VEC_BANK,    INTSL3
.equ ZP_HEXPCK_LO,            HXPKL
.equ ZP_HEXPCK_HI,            HXPKH
.equ ZP_PSEUDO_PC_LO,         PCLBCK
.equ ZP_PSEUDO_PC_HI,         PCHBCK
.equ ZP_PSEUDO_PSW,           PSWBCK
.equ ZP_PSEUDO_SP,            SPBCK
.equ ZP_PSEUDO_X,             XBCK
.equ ZP_PSEUDO_Y,             YBCK
.equ ZP_PSEUDO_A,             ABCK
.equ ZP_TEMP_LO,              MODADL
.equ ZP_TEMP_HI,              MODADH
.equ ZP_COPY_TEMP_LO,         COPL
.equ ZP_COPY_TEMP_HI,         COPH
.equ ZP_BREAKPOINT_ADDR_BASE, BPTLO
.equ ZP_BREAKPOINT_ADDR_END,  $002F
.equ ZP_BREAKPOINT_CODE_BASE, BPTCOD
.equ ZP_BREAKPOINT_CODE_END,  $003F

; ---------------------------------------------------------------------------
; ROM regions and documented TANBUG entry points
; ---------------------------------------------------------------------------
.equ BASIC_ROM_START,        $C000
.equ BASIC_ROM_END,          $E7FF
.equ XBUG_ROM_START,         $F000
.equ XBUG_ROM_END,           $F7FF
.equ TANBUG_ROM_START,       $F800
.equ TANBUG_ROM_END,         $FFFF

.equ TANBUG_POLLKB,          $FDFA
.equ TANBUG_OUTPCR,          $FE73
.equ TANBUG_OPCHR,           $FE75
.equ TANBUG_HEXPNT,          $FF0B
.equ TANBUG_HEXPCK,          $FF28

; Short aliases matching common monitor documentation.
.equ POLLKB,                 TANBUG_POLLKB
.equ OUTPCR,                 TANBUG_OUTPCR
.equ OPCHR,                  TANBUG_OPCHR
.equ HEXPNT,                 TANBUG_HEXPNT
.equ HEXPCK,                 TANBUG_HEXPCK

; ---------------------------------------------------------------------------
; Main display memory and mode controls
; ---------------------------------------------------------------------------
.equ DISPLAY_RAM_START,      $0200
.equ DISPLAY_RAM_END,        $03FF
.equ CHUNKY_ENABLE,          $BFF0 ; Read to enable for subsequent display writes
.equ CHUNKY_DISABLE,         $BFF3 ; Write to disable for subsequent display writes
.equ INVERSE_ENABLE,         $BFF4 ; Read to enable for subsequent display writes
.equ INVERSE_DISABLE,        $BFF7 ; Write to disable for subsequent display writes

; ---------------------------------------------------------------------------
; Mousepacket Colour VDU
; ---------------------------------------------------------------------------
.equ COLOUR_VDU_RAM_START,   $A000
.equ COLOUR_VDU_VISIBLE_END, $A63F
.equ COLOUR_VDU_RAM_END,     $A7EF
.equ COLOUR_VDU_LINE_WIDTH,  $A640 ; TANBUG: $20 standard, $40 Colour VDU
.equ COLOUR_VDU_STANDARD_WIDTH, $20
.equ COLOUR_VDU_COLOUR_WIDTH, $40
.equ COLOUR_VDU_CURSOR_FORMAT, $A641
.equ COLOUR_VDU_OUTPUT_CHANGE, $A642
.equ COLOUR_VDU_CRTC_SELECT, $A7FE
.equ COLOUR_VDU_CRTC_DATA,   $A7FF

; MC6845 register indices used by the Colour VDU.
.equ CRTC_H_TOTAL,           $00
.equ CRTC_H_DISPLAYED,       $01
.equ CRTC_H_SYNC_POSITION,   $02
.equ CRTC_SYNC_WIDTH,        $03
.equ CRTC_V_TOTAL,           $04
.equ CRTC_V_TOTAL_ADJUST,    $05
.equ CRTC_V_DISPLAYED,       $06
.equ CRTC_V_SYNC_POSITION,   $07
.equ CRTC_INTERLACE_MODE,    $08
.equ CRTC_SCANLINES,         $09
.equ CRTC_CURSOR_START,      $0A
.equ CRTC_CURSOR_END,        $0B
.equ CRTC_START_ADDRESS_HI,  $0C
.equ CRTC_START_ADDRESS_LO,  $0D
.equ CRTC_CURSOR_ADDRESS_HI, $0E
.equ CRTC_CURSOR_ADDRESS_LO, $0F

; ---------------------------------------------------------------------------
; AY-3-8910 PSG memory-mapped interface addresses
; ---------------------------------------------------------------------------
.equ AY1_ADDR,       $BC00
.equ AY1_DATA,       $BC01
.equ AY2_ADDR,       $BC02
.equ AY2_DATA,       $BC03
.equ AY3_ADDR,       $BC04
.equ AY3_DATA,       $BC05
.equ AY4_ADDR,       $BC06
.equ AY4_DATA,       $BC07
.equ AY5_ADDR,       $BC08
.equ AY5_DATA,       $BC09
.equ AY6_ADDR,       $BC0A
.equ AY6_DATA,       $BC0B
.equ AY7_ADDR,       $BC0C
.equ AY7_DATA,       $BC0D
.equ AY8_ADDR,       $BC0E
.equ AY8_DATA,       $BC0F
.equ INVADERS_SOUND, $BC80

; AY register indices (write index to AYx_ADDR, then data via AYx_DATA)
.equ AY_REG_A_FINE,     $00
.equ AY_REG_A_COARSE,   $01
.equ AY_REG_B_FINE,     $02
.equ AY_REG_B_COARSE,   $03
.equ AY_REG_C_FINE,     $04
.equ AY_REG_C_COARSE,   $05
.equ AY_REG_NOISE_PER,  $06
.equ AY_REG_ENABLE,     $07
.equ AY_REG_A_VOL,      $08
.equ AY_REG_B_VOL,      $09
.equ AY_REG_C_VOL,      $0A
.equ AY_REG_ENV_FINE,   $0B
.equ AY_REG_ENV_COARSE, $0C
.equ AY_REG_ENV_SHAPE,  $0D
.equ AY_REG_PORT_A,     $0E
.equ AY_REG_PORT_B,     $0F

; ---------------------------------------------------------------------------
; ETI real-time clock/calendar ($BC30-$BC3F)
; ---------------------------------------------------------------------------
.equ RTC_TEST,            $BC30
.equ RTC_TENTHS,          $BC31
.equ RTC_SECONDS_UNITS,   $BC32
.equ RTC_SECONDS_TENS,    $BC33
.equ RTC_MINUTES_UNITS,   $BC34
.equ RTC_MINUTES_TENS,    $BC35
.equ RTC_HOURS_UNITS,     $BC36
.equ RTC_HOURS_TENS,      $BC37
.equ RTC_DAY_UNITS,       $BC38
.equ RTC_DAY_TENS,        $BC39
.equ RTC_WEEKDAY,         $BC3A
.equ RTC_MONTH_UNITS,     $BC3B
.equ RTC_MONTH_TENS,      $BC3C
.equ RTC_YEAR_STATUS,     $BC3D
.equ RTC_START_STOP,      $BC3E
.equ RTC_INTERRUPT,       $BC3F

.equ RTC_INTERRUPTS_OFF,  $00
.equ RTC_IRQ_ONCE_60S,    $04
.equ RTC_IRQ_REPEAT_60S,  $0C
.equ RTC_IRQ_ONCE_5S,     $02
.equ RTC_IRQ_REPEAT_5S,   $0A
.equ RTC_IRQ_ONCE_HALF_S, $01
.equ RTC_IRQ_REPEAT_HALF, $09

; ---------------------------------------------------------------------------
; 6522 VIA register offsets
; ---------------------------------------------------------------------------
.equ VIA_ORB,   $00
.equ VIA_ORA,   $01
.equ VIA_DDRB,  $02
.equ VIA_DDRA,  $03
.equ VIA_T1C_L, $04
.equ VIA_T1C_H, $05
.equ VIA_T1L_L, $06
.equ VIA_T1L_H, $07
.equ VIA_T2C_L, $08
.equ VIA_T2C_H, $09
.equ VIA_SR,    $0A
.equ VIA_ACR,   $0B
.equ VIA_PCR,   $0C
.equ VIA_IFR,   $0D
.equ VIA_IER,   $0E
.equ VIA_ORA2,  $0F

; VIA base addresses
.equ VIA1_BASE, $BFC0
.equ VIA2_BASE, $BFE0

; VIA1 absolute register labels
.equ VIA1_ORB,   VIA1_BASE + VIA_ORB
.equ VIA1_ORA,   VIA1_BASE + VIA_ORA
.equ VIA1_DDRB,  VIA1_BASE + VIA_DDRB
.equ VIA1_DDRA,  VIA1_BASE + VIA_DDRA
.equ VIA1_T1C_L, VIA1_BASE + VIA_T1C_L
.equ VIA1_T1C_H, VIA1_BASE + VIA_T1C_H
.equ VIA1_T1L_L, VIA1_BASE + VIA_T1L_L
.equ VIA1_T1L_H, VIA1_BASE + VIA_T1L_H
.equ VIA1_T2C_L, VIA1_BASE + VIA_T2C_L
.equ VIA1_T2C_H, VIA1_BASE + VIA_T2C_H
.equ VIA1_SR,    VIA1_BASE + VIA_SR
.equ VIA1_ACR,   VIA1_BASE + VIA_ACR
.equ VIA1_PCR,   VIA1_BASE + VIA_PCR
.equ VIA1_IFR,   VIA1_BASE + VIA_IFR
.equ VIA1_IER,   VIA1_BASE + VIA_IER
.equ VIA1_ORA2,  VIA1_BASE + VIA_ORA2

; VIA2 absolute register labels
.equ VIA2_ORB,   VIA2_BASE + VIA_ORB
.equ VIA2_ORA,   VIA2_BASE + VIA_ORA
.equ VIA2_DDRB,  VIA2_BASE + VIA_DDRB
.equ VIA2_DDRA,  VIA2_BASE + VIA_DDRA
.equ VIA2_T1C_L, VIA2_BASE + VIA_T1C_L
.equ VIA2_T1C_H, VIA2_BASE + VIA_T1C_H
.equ VIA2_T1L_L, VIA2_BASE + VIA_T1L_L
.equ VIA2_T1L_H, VIA2_BASE + VIA_T1L_H
.equ VIA2_T2C_L, VIA2_BASE + VIA_T2C_L
.equ VIA2_T2C_H, VIA2_BASE + VIA_T2C_H
.equ VIA2_SR,    VIA2_BASE + VIA_SR
.equ VIA2_ACR,   VIA2_BASE + VIA_ACR
.equ VIA2_PCR,   VIA2_BASE + VIA_PCR
.equ VIA2_IFR,   VIA2_BASE + VIA_IFR
.equ VIA2_IER,   VIA2_BASE + VIA_IER
.equ VIA2_ORA2,  VIA2_BASE + VIA_ORA2

; ---------------------------------------------------------------------------
; Tangerine hi-res graphics board (as emulated in microtan65)
; ---------------------------------------------------------------------------
.equ HIRES_BASE,         $8000
.equ HIRES_END,          $9FFF
.equ HIRES_BANK_SELECT,  $FFFF

; Values written to HIRES_BANK_SELECT
.equ HIRES_BANK_RED,       $01
.equ HIRES_BANK_GREEN,     $02
.equ HIRES_BANK_BLUE,      $03
.equ HIRES_BANK_INTENSITY, $04
