# TANDOS disk images

The emulator mounts raw, single-sided TANDOS disk images as logical units
`0:` through `7:`. Images contain 256-byte sectors in track and sector order.
Supported geometries are 35 to 80 tracks with either 9 or 10 sectors per
track.

`tandos_master.img` is logical unit `0:` extracted without modification from
the published Microtan TANDOS HxC master image. It is an 80-track,
10-sector-per-track image. Mount it read-only to protect the original.

Use `F1`, select `Disks`, then choose a unit to mount or eject an image. The
same menu can create a blank image. Card state and mounted image paths are
saved in `microtan_settings.txt`.
