# asm6502.py

`asm6502.py` is a 2-pass 6502/65C02 assembler for `microtan65`.

It reads opcode/mode information from `cpu_6502.c`, so generated machine code matches the emulator instruction table.

## Usage

From the repository root:

```bash
python3 tools/asm6502.py <input.asm> [-f hex|bin] [-o output_file]
```

Examples:

```bash
python3 tools/asm6502.py examples/fill_0200_03ff.s
python3 tools/asm6502.py examples/fill_0200_03ff.s -f bin -o fill.bin
```

Default output format is Intel HEX (`-f hex`).

## Directives

- `.org <expr>`: set program counter/address
- `.equ name, <expr>`: define symbol
- `name = <expr>`: define symbol
- `.byte <items...>`: emit bytes
- `.word <items...>`: emit 16-bit little-endian words
- `.text <items...>` / `.ascii <items...>`: emit text bytes
- `.fill count[, value]`: emit repeated byte values
- `.include "path"`: include another source file
- `.if <expr>` / `.elif <expr>` / `.else` / `.endif`: conditional assembly
- `.ifdef <symbol>` / `.ifndef <symbol>`: symbol-defined conditionals

### Include Notes

- Include paths are resolved relative to the file that contains the `.include` line.
- Nested includes are supported.
- Include cycles are detected and reported as an error.

### Conditional Assembly

- `.if <expr>` evaluates an expression; non-zero is true (use `==` for equality, not `=`).
- `.elif <expr>` and `.else` select alternate branches.
- `.ifdef <symbol>` is true when a symbol is already defined.
- `.ifndef <symbol>` is true when a symbol is not defined.
- Blocks can be nested.

```asm
.if TARGET == 1
    lda #$01
.elif TARGET == 2
    lda #$02
.else
    lda #$00
.endif
```

## Expressions

Expressions support decimal plus these common formats:

- Hex: `$1234`
- Binary: `%10101010`
- Character literals: `'A'`

Supported operators (integer arithmetic):

- `+`, `-`, `*`, `/`, `%`
- `<<`, `>>`
- `&`, `|`, `^`, `~`
- Parentheses: `( ... )`

Notes:

- `/` uses integer division semantics.
- Expressions can be used in directives and instruction operands.

## Labels and Branches

- Labels are supported (`label:`), including forward references.
- Branch instructions use relative offsets and are range-checked.
- Symbol names are treated case-insensitively.

### Local Labels

- Local labels use `@name:` syntax and are scoped to the most recent non-local label.
- Local references use `@name`.
- Local labels can be reused under different global labels.

```asm
Main:
@loop:
    dex
    bne @loop

Other:
@loop:
    iny
    bne @loop
```

## Tables

Use the data directives to build lookup tables and jump tables:

```asm
BitMaskTable:
    .byte $01, $02, $04, $08, $10, $20, $40, $80

JumpTable:
    .word FuncA, FuncB, FuncC
```

## Addressing Syntax

Use standard 6502 syntax, for example:

- `LDA #$20`
- `STA $44`
- `STA $1234`
- `LDA ($20,X)`
- `LDA ($20),Y`
- `JMP ($1234)`
- `JMP ($1234,X)`

The assembler picks zero-page vs absolute forms automatically when both are available.

## Errors

Assembler errors are reported with source location (`file:line`) where possible, including when code comes from included files.

## Output Notes

- `hex`: writes Intel HEX records.
- `bin`: writes a contiguous binary image from the lowest emitted address to the highest.


## Microtan Hardware Labels

A ready-made include file for AY8910 and 6522 labels is provided at:

- `examples/includes/microtan_hw_labels.inc`

It defines:

- TANBUG/XBUG zero-page labels (`ZP_*`)
- Microtan manual zero-page labels (`NULL`, `ICHAR`, `VDUIND`, `PCLBCK`, `BPTLO`, etc.)
- ROM region labels (`*_ROM_START`, `*_ROM_END`)
- TANBUG routine entry labels (`TANBUG_*`)
- AY chip interface addresses (`AY1_ADDR`, `AY1_DATA`, `AY2_ADDR`, `AY2_DATA`)
- AY register indices (`AY_REG_*`)
- VIA register offsets (`VIA_*`)
- Absolute VIA register labels for both chips (`VIA1_*`, `VIA2_*`)

Example:

```asm
.include "../includes/microtan_hw_labels.inc"

.org $0400
Start:
    lda #$ff
    sta VIA1_DDRB

    lda #AY_REG_ENABLE
    sta AY1_ADDR
    lda #$3f
    sta AY1_DATA

    brk
```
