# Library Sources

## `hires_graphics_lib.s`

Fast 6502 helper routines for Microtan hi-res RGBI graphics in `microtan65`.

- Hi-res pixel window: `$8000-$9FFF`
- Plane select register: `$FFFF`
  - `1=Red`, `2=Green`, `3=Blue`, `4=Intensity`
- Colour format used by the library: low nibble `IRGB` bits in `A`/`HRG_COLOR`
  - bit0 red, bit1 green, bit2 blue, bit3 intensity

### Entry Points

- `HRG_ClearScreenA` : clear full hi-res screen to colour in `A`
- `HRG_SetPixelXYA` : set pixel at `X,Y` to colour in `A`
- `HRG_SetPixelXYSafeA` : same as above, preserves `X,Y`
- `HRG_DrawLine` : line using `HRG_X0,HRG_Y0 -> HRG_X1,HRG_Y1`
- `HRG_DrawRect` : rectangle outline using `HRG_X0,HRG_Y0 -> HRG_X1,HRG_Y1`
- `HRG_FillRect` : filled rectangle using `HRG_X0,HRG_Y0 -> HRG_X1,HRG_Y1`
- `HRG_DrawTriangle` : outline triangle using vertices `HRG_(X0,Y0),(X1,Y1),(X2,Y2)`
- `HRG_FillTriangle` : scanline-filled triangle, same vertex frame
- `HRG_DrawEllipse` : 64-segment ellipse outline from bounding box `HRG_X0,HRG_Y0 -> HRG_X1,HRG_Y1`
- `HRG_FillEllipse` : filled ellipse (opposite-point chord fill), same bounding box frame

### Zero Page Usage

The library uses a fixed caller-owned zero-page frame (`$A0-$D7`).

### Example

See `examples/hires_graphics_demo.s`.

Before running the demo, select **Tangerine hi-res** in the emulator display menu.
