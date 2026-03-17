; directives_demo.s
; Exercises .byte/.word/.text/.ascii/.fill and expressions.

.org $0600

Constants:
    .byte $00, %10101010, 'A', 10 + 6

Pointers:
    .word Constants, Text1, $BEEF

Text1:
    .text "HELLO", 0

Text2:
    .ascii "RAW", $0D, $0A

Padding:
    .fill 16, $FF

Done:
    brk
