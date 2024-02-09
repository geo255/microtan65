#ifndef __SYSTEM_H__
#define __SYSTEM_H__

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

// Callback function prototypes
typedef uint8_t (*memory_read_callback)(uint16_t address);
typedef void (*memory_write_callback)(uint16_t address, uint8_t value);
typedef int (*device_intialisation_callback)(uint8_t bank, uint16_t address, uint16_t param, char *identifier);
typedef void (*device_reset_callback)(uint8_t bank, uint16_t address);
typedef void (*device_close_callback)();

// Device configuration information
// * Function to call to add a device to the system
// * Backplane bank (card slot) where the device is located
// * Start address of the device
// * Additional address
// * Additional parameters
typedef struct 
{
    device_intialisation_callback initialiser;
    device_reset_callback reset;
    device_close_callback close;
    uint8_t bank;
    uint16_t address;
    uint16_t param;
    char *identifier;
} *device_configuration_ptr_t, device_configuration_t;

// Memory-mapped device structure
typedef struct 
{
    uint16_t start_address;
    uint16_t end_address;
    memory_read_callback read;
    memory_write_callback write;
    bool use_main_ram;
} memory_mapped_device_t;

extern int system_initialise();
extern void system_reset();
extern void system_set_read_only(uint16_t start_address, uint16_t end_address);
extern int system_register_memory_mapped_device(uint16_t start, uint16_t end, memory_read_callback read_cb, memory_write_callback write_cb, bool use_main_ram);
extern uint8_t system_read_memory(uint16_t address);
extern void system_write_memory(uint16_t address, uint8_t value);
extern uint8_t *system_get_memory_pointer(uint16_t address);
extern int system_load_m65_file(char *file_name);
extern void system_close();

#endif //__SYSTEM_H__
