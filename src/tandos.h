#ifndef __TANDOS_H__
#define __TANDOS_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define TANDOS_UNIT_COUNT 8

extern int tandos_initialise(uint8_t bank, uint16_t address, uint16_t param, char* identifier);
extern void tandos_reset(uint8_t bank, uint16_t address);
extern void tandos_close(void);
extern void tandos_update(uint32_t elapsed_cycles);

extern void tandos_set_enabled(bool enabled);
extern bool tandos_get_enabled(void);

extern int tandos_mount(int unit, const char* file_name, bool write_protected);
extern void tandos_eject(int unit);
extern void tandos_eject_all(void);
extern bool tandos_unit_mounted(int unit);
extern bool tandos_unit_write_protected(int unit);
extern const char* tandos_unit_file_name(int unit);
extern int tandos_unit_tracks(int unit);
extern int tandos_unit_sectors_per_track(int unit);
extern int tandos_create_image(const char* file_name, int tracks, int sectors_per_track);

extern void tandos_save_settings(FILE* file);
extern void tandos_load_settings(FILE* file);

#endif // __TANDOS_H__
