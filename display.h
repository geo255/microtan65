#ifndef __DISPLAY_H__
#define __DISPLAY_H__

#include <stdbool.h>
#include <stdint.h>
#include "system.h"

#define DISPLAY_WIDTH       256
#define DISPLAY_HEIGHT      256

typedef enum
{
    DISPLAY_HIRES_MODE_NONE,
    DISPLAY_HIRES_MODE_TANGERINE,
    DISPLAY_HIRES_MODE_EXTENDED
} display_hires_mode_t;

extern void display_render(uint32_t *pixels);
extern bool display_updated_event();
extern uint8_t *display_get_hires_memory_pointer(int board_index);
extern void display_set_hires_mode(display_hires_mode_t new_mode);
extern display_hires_mode_t display_get_hires_mode();
extern void display_load_chunky_memory(uint8_t *src);
extern void display_gpu_set_colour(uint8_t x, uint8_t y, uint8_t colour);
extern uint8_t display_gpu_get_colour(uint8_t x, uint8_t y);
extern uint8_t display_hrg_function(uint8_t px, uint8_t py, uint8_t function);
extern int display_initialise(uint8_t bank, uint16_t address, uint16_t param, char *identifier);
extern void display_close();

#endif // __DISPLAY_H__
