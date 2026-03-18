# GPU Register Spec

This is the active GPU API for microtan65 (unreleased, no version split).
Source of truth: `src/display.c` and `src/system.c`.

## Memory Map

- Base: `$BF00`
- Range: `$BF00-$BF7F` (128 registers)
- Register index: `reg = address - $BF00`

## Core Registers

- `$00`: Draw colour index (0-255)
- `$01-$0C`: Command parameters
- `$1D`: Result 0 (used by pixel read)
- `$1E`: Result 1 (reserved for command results)
- `$1F`: Error detail register
- `$20`: Collision query sprite id
- `$21`: Collision count
- `$22-$61`: Collision sprite id list (`$FF` unused)
- `$7B`: Plane write mask (low nibble, replicated to both colour nibbles)
- `$7C`: Plane display mask (`0` hides GPU framebuffer, non-zero shows it)
- `$7D`: Random byte (updated on every GPU register read)
- `$7E`: Command status register
- `$7F`: Operation register (write opcode to execute)

## Status / Error Codes

Written to `$7E` on command dispatch and to `$1F` by stamp/sprite helpers.

- `$00`: OK
- `$01`: Source memory range overflow
- `$02`: Allocation/missing resource
- `$03`: Invalid id
- `$04`: Inactive/disabled sprite
- `$05`: Unknown opcode

## Opcodes

- `$00` Set pixel: `$00=colour`, `$01=x`, `$02=y`
- `$01` Get pixel: `$01=x`, `$02=y` -> `$1D=colour`
- `$10` Draw line: `$00=colour`, `$01,$02=x1,y1`, `$03,$04=x2,y2`
- `$11` Draw line-to: same as `$10`, then `$01,$02 <- $03,$04`
- `$20` Draw triangle outline: `$00=colour`, `$01..$06` are 3 XY pairs
- `$21` Fill triangle: same parameters as `$20`
- `$30` Draw rectangle outline: `$00=colour`, `$01,$02=x1,y1`, `$03,$04=x2,y2`
- `$31` Fill rectangle: same as `$30`
- `$40` Draw ellipse outline: `$00=colour`, `$01,$02=x1,y1`, `$03,$04=x2,y2`
- `$41` Fill ellipse: same as `$40`
- `$80` Create stamp: `$01=id`, `$02=width`, `$03=height`, `$04/$05=src lo/hi`
- `$81` Place stamp: `$01=id`, `$02/$03=x lo/hi (signed)`, `$04/$05=y lo/hi (signed)`
- `$90` Create sprite:
  - `$01=id`
  - `$02/$03=x lo/hi (signed)`
  - `$04/$05=y lo/hi (signed)`
  - `$06=group`
  - `$07=collision_group`
  - `$08=flags`
  - `$09=width`, `$0A=height`
  - `$0B/$0C=image src lo/hi`
- `$91` Move sprite: `$01=id`, `$02/$03=x lo/hi`, `$04/$05=y lo/hi`
- `$92` Set sprite flags: `$01=id`, `$02=flags`
- `$93` Detect collisions: `$01=id` -> `$21=count`, `$22..` sprite ids
- `$E0` Scroll: `$00=fill colour`, `$01=dx signed`, `$02=dy signed`
- `$F0` Border: `$01=left`, `$02=top`, `$03=right`, `$04=bottom`

## Sprite Flags

- Bit 0: enabled
- Bit 1: visible

Notes:
- Sprite rendering now requires active + enabled + visible.
- Collision detection requires active + enabled for both sprites.
- Transparency key is palette index `$FF`.

## Rendering Behaviour

- GPU framebuffer is indexed 8-bit colour (`256x256`).
- Palette maps index to RGB.
- In GPU mode, text/chunky overlay is still composited on top.
- Sprite blit is clipped to display bounds.

## Defaults at GPU Init

- `$7B` (write mask) = `$0F`
- `$7C` (display mask) = `$01`
- `$7E` (status) = `$00`
- `$1F` (error detail) = `$00`
