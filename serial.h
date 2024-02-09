#ifndef __SERIAL_H__
#define __SERIAL_H__

#include "system.h"

extern int serial_initialise(uint8_t bank, uint16_t address, uint16_t param, char *identifier);
extern void serial_reset(uint8_t bank, uint16_t address);

#endif // __SERIAL_H__