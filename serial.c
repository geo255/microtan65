#include <string.h>
#include "serial.h"

static uint8_t serial_registers[4];

void serial_write_callback(uint16_t address, uint8_t value)
{
}

uint8_t serial_read_callback(uint16_t address)
{
    return serial_registers[address & 0x03];
}

void serial_reset(uint8_t bank, uint16_t address)
{
    memset(serial_registers, 0xff, sizeof(serial_registers));
}


int serial_initialise(uint8_t bank, uint16_t address, uint16_t param, char *identifier)
{
    system_register_memory_mapped_device(address, address + 0x03, serial_read_callback, serial_write_callback, false);
}

