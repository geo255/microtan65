#ifndef __VIA_6522_H__
#define __VIA_6522_H__

#include <stdbool.h>
#include <stdint.h>
#include "system.h"

typedef enum
{
    via_6522_clear,
    via_6522_set,
    via_6522_write_all
} via_6522_port_operation_t;

extern void via_6522_reset(uint8_t bank, uint16_t address);
extern bool via_6522_update(int pnTicks);
extern void via_6522_set_input_port(int device_index, int port_index, via_6522_port_operation_t operation, uint16_t port_value);
extern void via_6522_write_register(int device_index, int register_index, uint8_t register_value);
extern uint8_t via_6522_read_register(int pnDevice, int pnReg);
extern uint8_t via_6522_read_output_port(int pnDevice, int pnPort);
extern void via_6522_reload();
extern int via_6522_initialise(uint8_t bank, uint16_t address, uint16_t param, char *identifier);
extern void via_6522_print_regs();

#endif // __VIA_6522_H__