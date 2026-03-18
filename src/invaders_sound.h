#ifndef __INVADERS_SOUND_H__
#define __INVADERS_SOUND_H__

#include <stdint.h>

/* Initialize Space Invaders sound effects system.
 * Returns 0 on success, -1 on failure (sound will be disabled but emulator continues) */
extern int invaders_sound_initialise(uint8_t bank, uint16_t address, uint16_t param, char* identifier);

/* Cleanup and free all sound resources */
extern void invaders_sound_close(void);

/* Reset sound state (stop all sounds, reset tracking variables) */
extern void invaders_sound_reset(uint8_t bank, uint16_t address);

#endif /* __INVADERS_SOUND_H__ */
