#ifndef __COLOUR_VDU_H__
#define __COLOUR_VDU_H__

#include <stdbool.h>
#include <stdint.h>

#define COLOUR_VDU_WIDTH  384
#define COLOUR_VDU_HEIGHT 250

extern int colour_vdu_initialise(uint8_t bank, uint16_t address, uint16_t param, char* identifier);
extern void colour_vdu_reset(uint8_t bank, uint16_t address);
extern void colour_vdu_set_enabled(bool enabled);
extern bool colour_vdu_get_enabled(void);
extern bool colour_vdu_output_changed_event(void);
extern bool colour_vdu_output_selected(void);
extern bool colour_vdu_updated_event(void);
extern void colour_vdu_render(uint32_t* pixels);

#endif // __COLOUR_VDU_H__
