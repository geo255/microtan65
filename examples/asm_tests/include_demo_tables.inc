; include_demo_tables.inc
; Data tables used by include_demo_main.s.

Message:
    .text "INCLUDE TEST", 0

BitMaskTable:
    .byte $01, $02, $04, $08, $10, $20, $40, $80

WordTable:
    .word $1234, $ABCD, Message
