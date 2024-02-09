#ifndef __EPROM_H__
#define __EPROM_H__

#include "system.h"

extern int eprom_initialise(uint8_t bank, uint16_t address, uint16_t param, char *identifier);

#endif // __EPROM_H__
