#include <string.h>

#include "cpu_6502.h"
#include "display.h"
#include "eprom.h"
#include "function_return_codes.h"
#include "keyboard.h"
#include "serial.h"
#include "system.h"
#include "via_6522.h"

#define MAX_DEVICES 32
memory_mapped_device_t devices[MAX_DEVICES];
static int device_count = 0;
static uint8_t system_memory[65536];
static uint8_t memory_read_only[65536];

// List of system devices
device_configuration_t system_devices[] =
{
    { keyboard_initialise,  NULL,           NULL,           0x00,  0xbff0, 0x0000, "keyboard"        },
    { display_initialise,   NULL,           NULL,           0x00,  0x0200, 0xbff0, "main display"    },
    { display_initialise,   NULL,           NULL,           0x01,  0x8000, 0x0000, "hires red"       },
    { display_initialise,   NULL,           NULL,           0x02,  0x8000, 0x0000, "hires green"     },
    { display_initialise,   NULL,           NULL,           0x03,  0x8000, 0x0000, "hires blue"      },
    { display_initialise,   NULL,           NULL,           0x04,  0x8000, 0x0000, "hires intensity" },
    { display_initialise,   NULL,           display_close,  0x00,  0xbf00, 0x0000, "gpu"             },
    { via_6522_initialise,  via_6522_reset, NULL,           0x00,  0xbfc0, 0x0000, NULL              },
    { via_6522_initialise,  via_6522_reset, NULL,           0x00,  0xbfe0, 0x0000, NULL              },
    { serial_initialise,    serial_reset,   NULL,           0x00,  0xbfd0, 0xbfd3, NULL              },
    { eprom_initialise,     NULL,           NULL,           0x00,  0xc000, 0x0000, "microtan.rom"    },
    { cpu_6502_initialise,  cpu_6502_reset, NULL,           0x00,  0x0000, 0x0000, NULL              },
    { NULL,                 NULL,           NULL,           0x00,  0x0000, 0x0000, NULL              }
};

// Register a memory-mapped device
int system_register_memory_mapped_device(uint16_t start, uint16_t end, memory_read_callback read_cb, memory_write_callback write_cb, bool use_main_ram)
{
    if (device_count == MAX_DEVICES)
    {
        printf("system_register_device(%04x, %04x): failed\r\n", start, end);
        return RV_DEVICE_NOT_ADDED;
    }

    devices[device_count].start_address = start;
    devices[device_count].end_address = end;
    devices[device_count].read = read_cb;
    devices[device_count].write = write_cb;
    devices[device_count].use_main_ram = use_main_ram;
    device_count++;
    return RV_OK;
}

// Memory read function
uint8_t system_read_memory(uint16_t address)
{
    bool registered = false;
    uint8_t rv = 0;

    for (int i = 0; i < device_count; i++)
    {
        if ((address >= devices[i].start_address) && (address <= devices[i].end_address))
        {
            if (NULL != devices[i].read)
            {
                rv |= devices[i].read(address);
            }

            if (devices[i].use_main_ram)
            {
                rv |= system_memory[address];
            }

            registered = true;
        }
    }

    if (registered)
    {
        return rv;
    }

    return system_memory[address];
}

// Memory write function
void system_write_memory(uint16_t address, uint8_t value)
{
    for (int i = 0; i < device_count; i++)
    {
        if (address >= devices[i].start_address && address <= devices[i].end_address)
        {
            if (devices[i].use_main_ram)
            {
                system_memory[address] = value;
            }

            if (NULL != devices[i].write)
            {
                devices[i].write(address, value);
            }
        }
    }

    if (!memory_read_only[address])
    {
        system_memory[address] = value;
    }
}

uint8_t *system_get_memory_pointer(uint16_t address)
{
    return system_memory + address;
}

void system_set_read_only(uint16_t start_address, uint16_t end_address)
{
    memset(memory_read_only + start_address, 0x01, end_address - start_address + 1);
}

int system_load_m65_file(char *file_name)
{
    FILE *m65_file = fopen(file_name, "rb");

    if (!m65_file)
    {
        printf("Error opening [%s]\r\n", file_name);
        return RV_FILE_OPEN_ERROR;
    }

    fseek(m65_file, 0, SEEK_END);
    size_t m65_file_size = ftell(m65_file);
    fseek(m65_file, 0, SEEK_SET);
    uint8_t *system_memory = system_get_memory_pointer(0x0000);

    if (m65_file_size != 8263)
    {
        uint8_t ay8910_registers[32];
        uint8_t chunky_state;
        bool extended = true;
        uint16_t file_version;
        uint16_t memory_size;
        fread(&file_version, 1, sizeof(file_version), m65_file);
        fread(&memory_size, 1, sizeof(memory_size), m65_file);
        fread(system_memory, 1, memory_size, m65_file);
        fread(system_memory + 0xbfc0, 1, 16, m65_file);
        fread(system_memory + 0xbfe0, 1, 16, m65_file);
        fread(system_memory + 0xbff0, 1, 16, m65_file);
        fread(system_memory + 0xbc04, 1, 1, m65_file);
        fread(&chunky_state, 1, 1, m65_file);
        fread(ay8910_registers, 1, 32, m65_file);

        if (chunky_state)
        {
            system_read_memory(0xbff0);
        }
        else
        {
            system_write_memory(0xbff3, 0);
        }

        if (file_version >= 1)
        {
            fread(display_get_hires_memory_pointer(0), 1, 8192, m65_file);
            fread(display_get_hires_memory_pointer(1), 1, 8192, m65_file);
            fread(display_get_hires_memory_pointer(2), 1, 8192, m65_file);
            fread(display_get_hires_memory_pointer(3), 1, 8192, m65_file);
        }

        // Write AY8910 registers here
        via_6522_reload();
    }
    else
    {
        fread(system_memory, 1, 0x2000, m65_file);
    }

    uint8_t chunky_graphics_memory[512];
    uint8_t *chunky = chunky_graphics_memory;
    uint8_t b;

    for (int i = 0; i < 0x40; i++)
    {
        fread(&b, 1, 1, m65_file);

        for (int bit = 0; bit < 8; b >>= 1, bit++)
        {
            *chunky++ = (b & 1);
        }
    }

    display_load_chunky_memory(chunky_graphics_memory);
    uint8_t status[7];
    fread(status, 1, sizeof(status), m65_file);
    fclose(m65_file);
    cpu_6502_continue((uint16_t)status[0] | (uint16_t)status[1] << 8, status[3], status[4], status[5], status[6], status[2]);
    return RV_OK;
}


void system_reset()
{
    device_configuration_ptr_t device = system_devices;

    while (NULL != device->initialiser)
    {
        if (NULL != device->reset)
        {
            device->reset(device->bank, device->address);
        }

        device++;
    }
}

int system_initialise()
{
    memset(memory_read_only, 0, sizeof(memory_read_only));
    device_configuration_ptr_t device = system_devices;

    while (NULL != device->initialiser)
    {
        device->initialiser(device->bank, device->address, device->param, device->identifier);
        device++;
    }

    system_reset();
    return RV_OK;
}

void system_close()
{
    device_configuration_ptr_t device = system_devices;

    while (NULL != device->initialiser)
    {
        if (NULL != device->close)
        {
            device->close();
        }

        device++;
    }
}

