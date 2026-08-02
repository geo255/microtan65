# ETI real-time clock emulation

The emulator implements the ETI real-time clock/calendar card described in
*Electronics Today International*, April 1983. The card is mapped at the
article's example address, `$BC30-$BC3F`.

## Registers

| Address | Register | Access |
| --- | --- | --- |
| `$BC30` | Test control | Write |
| `$BC31` | Tenths of seconds | Read |
| `$BC32` | Units of seconds | Read |
| `$BC33` | Tens of seconds | Read |
| `$BC34` | Units of minutes | Read/write |
| `$BC35` | Tens of minutes | Read/write |
| `$BC36` | Units of hours | Read/write |
| `$BC37` | Tens of hours | Read/write |
| `$BC38` | Units of day of month | Read/write |
| `$BC39` | Tens of day of month | Read/write |
| `$BC3A` | Day of week, 1=Monday through 7=Sunday | Read/write |
| `$BC3B` | Units of month | Read/write |
| `$BC3C` | Tens of month | Read/write |
| `$BC3D` | Leap-year cycle status | Write |
| `$BC3E` | Start/stop, 1=start and 0=stop | Write |
| `$BC3F` | Interrupt mode | Read/write |

Values are one BCD digit per register. As on the original board, seconds are
cleared when the clock is started.

## Interrupts

Writing register 15 selects an interrupt period and whether it fires once or
repeats:

| Value | Mode |
| --- | --- |
| `$00` | Disabled |
| `$01` | One interrupt after 0.5 seconds |
| `$02` | One interrupt after 5 seconds |
| `$04` | One interrupt after 60 seconds |
| `$09` | Repeating 0.5-second interrupts |
| `$0A` | Repeating 5-second interrupts |
| `$0C` | Repeating 60-second interrupts |

When an interrupt is pending, reading register 15 returns `$01`, `$02`, or
`$04` to identify its period. Read register 15 three times to acknowledge the
interrupt. In repeating mode, the next interval starts from the third read.
Until acknowledged, the RTC interrupt output remains asserted.

## Host-time model

With the default offset of zero, the RTC follows the host's local time. Writing
time or date registers recalculates the difference between the requested RTC
time and host time. Software can stop the clock, write several fields, and
restart it to commit the complete value atomically.

The signed offset is saved in `microtan_settings.txt` when the emulator exits
normally and restored on the next run. A Microtan reset does not reset the
battery-backed clock.

The original hardware has no year register. Register 13 only records the
four-year leap-year phase, so setting the date retains the current emulated
year.

## Update latch

The hardware marks an update every tenth of a second. The first read from any
RTC address after an update returns `$0F` and clears the indication, regardless
of which register was selected. Subsequent reads return normal values until the
next update. This permits the published BASIC routine to take a consistent
clock snapshot.

A slow monitor memory dump is not a coherent clock snapshot: it can display
`$0F` at different addresses as successive tenth-second updates occur. Software
should restart a read sequence when it encounters `$0F`, or compare tenths of
seconds before and after reading the remaining fields.

## Sources

- ETI, "Real Time Clock/Calendar", April 1983, pages 31-34:
  <http://www.microtan.ukpc.net/RTC.pdf>

Local copies are archived under `docs/official/rtc/`.
