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

Expressions support decimal plus these common formats:

- Hex: `$1234`
- Binary: `%10101010`
- Character literals: `'A'`

## Labels and Branches

- Labels are supported (`label:`), including forward references.
- Branch instructions use relative offsets and are range-checked.
- Symbol names are treated case-insensitively.

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

## Output Notes

- `hex`: writes Intel HEX records.
- `bin`: writes a contiguous binary image from the lowest emitted address to the highest.
