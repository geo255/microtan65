# TANDOS emulation

The optional TANDOS card provides its original memory map:

| Address | Function |
| --- | --- |
| `$A800-$AFFF` | DBASIC and record-file ROM routines |
| `$B000-$B7FF` | TANDOS operating-system ROM |
| `$B800-$BBFF` | 1 KiB TANDOS board RAM |
| `$BF90` | FDC command/status |
| `$BF91` | FDC track |
| `$BF92` | FDC sector |
| `$BF93` | FDC data |
| `$BF94` | TANDOS control/status |

Disabling the card exposes ordinary system RAM in these ranges. The supplied
2732 image needs a hardware-accurate half swap: TANDOS occupies the physical
lower 2 KiB and DBASIC the physical upper 2 KiB because the EPROM does not see
CPU address line A12.

## Disk images

The emulator currently mounts raw, single-sided images on TANDOS logical units
`0:` through `7:`. It supports:

- 256-byte sectors
- 35 to 80 tracks
- 9 or 10 sectors per track
- Read/write and read-only mounts
- Immediate 256-byte sector write-through
- Hot mounting and ejection
- Persistent card state and mounts

The first sector in an image is track 0, sector 1. Sectors are consecutive
within each track.

`disks/tandos_master.img` is unit `0:` extracted from the official
`DSKA0000.HFE` master image using HxCFloppyEmulator v2.16.10.1. All source HFE
sectors passed the converter's CRC checks. Mount this image read-only.

## Controller coverage

Implemented WD1793/MB8877 operations include RESTORE, SEEK, STEP, STEP IN,
STEP OUT, READ SECTOR, WRITE SECTOR, READ ADDRESS, and FORCE INTERRUPT.
TANDOS drive/side selection, DRQ, INTRQ, head-load status, not-ready status,
record-not-found status, and write protection are represented.

Low-level READ TRACK and WRITE TRACK are not yet implemented. Consequently,
the original FORMAT utility cannot lay out a new raw image. The emulator's
`Create blank disk image` action creates the sector structure directly; use
TANDOS INIT to add a filesystem.

Direct `.hfe` mounting, host-directory-backed disks, and host file
import/export remain future work.

## Sources

- TANDOS product archive: <https://www.microtan.ukpc.net/pageProducts.html>
- TANDOS reconstruction manual: <https://www.microtan.ukpc.net/Microtan-R/TANDOSManual.pdf>
- TANDOS disk anatomy: <https://www.microtan.ukpc.net/Products/DiskAnatomy.pdf>
- HxC master-image instructions: <https://www.microtan.ukpc.net/Microtan-R/TANDOSManualAppB.pdf>
- HxC converter: <https://github.com/jfdelnero/HxCFloppyEmulator>
