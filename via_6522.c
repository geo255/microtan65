#include <string.h>
#include "cpu_6502.h"
#include "function_return_codes.h"
#include "system.h"
#include "via_6522.h"

#define MAX_DEVICES             16

#define ORB                     0
#define ORA                     1
#define DDRB                    2
#define DDRA                    3
#define T1C_L                   4
#define T1C_H                   5
#define T1L_L                   6
#define T1L_H                   7
#define T2C_L                   8
#define T2C_H                   9
#define SR                      10
#define ACR                     11
#define PCR                     12
#define IFR                     13
#define IER                     14
#define ORA2                    15

static uint16_t address_table[MAX_DEVICES] = { 0xbfc0, 0xbfe0 };
static uint8_t via_6522_registers[MAX_DEVICES][16];
static uint16_t port_in[MAX_DEVICES][2];
static uint16_t port_out[MAX_DEVICES][2];
static int number_of_devices = 0;
static const uint8_t flag_clear_read_table[] = {(uint8_t)~0xc0, ~0x03, 0xff, 0xff, ~0x40, 0xff, 0xff, 0xff, ~0x20, 0xff, ~0x04, 0xff, 0xff, 0xff, 0xff, ~0x03};
static const uint8_t flag_clear_write_table[] = {(uint8_t)~0xc0, ~0x03, 0xff, 0xff, 0xff, 0xff, 0xff, ~0x40, 0xff, ~0x20, ~0x04, 0xff, 0xff, 0xff, 0xff, ~0x03};

#define MAX_ENTRIES 100

void address_print(uint16_t port_address)
{
    static uint16_t printedValues[MAX_ENTRIES];
    static size_t numPrintedValues = 0;
    uint16_t value = cpu_6502_get_pc();

    for (size_t i = 0; i < numPrintedValues; i++)
    {
        if (printedValues[i] == value)
        {
            return;
        }
    }

    if (numPrintedValues < MAX_ENTRIES)
    {
        printf("PC: %04x 6522 read %04x\r\n", cpu_6502_get_pc(), port_address);
        printedValues[numPrintedValues] = value;
        numPrintedValues++;
    }
}

void via_6522_print_regs()
{
    printf("\r");

    for (int i = 0; i < 16; i++)
    {
        printf("%02x ", via_6522_read_register(0, i));
    }
}

bool via_6522_update(int cpu_tick_count)
{
    long t1;
    long t2;
    long t1_low;
    bool t1_timed_out;
    bool rv = false;
    int n;

    for (n = 0; n < number_of_devices; n++)
    {
        t1 = via_6522_registers[n][T1C_L] + ((uint16_t)via_6522_registers[n][T1C_H] << 8);
        t1_timed_out = false;

        if (via_6522_registers[n][ACR] & (1 << 6))
        {
            t1 -= cpu_tick_count;
            t1_low = via_6522_registers[n][T1L_L] + ((uint16_t)via_6522_registers[n][T1L_H] << 8);

            if (t1 < 0)
            {
                while (t1 < 0)
                {
                    t1 += t1_low + 1;
                }

                t1_timed_out = true;

                if (via_6522_registers[n][ACR] & (1 << 7))
                {
                    port_out[n][1] ^= 0x80;
                }
            }
        }
        else /* Single shot */
        {
            if (t1 > 0)
            {
                t1 -= cpu_tick_count;

                if (t1 < 0)
                {
                    t1 = 0;
                    t1_timed_out = true;

                    if (via_6522_registers[n][ACR] & (1 << 7))
                    {
                        port_out[n][1] |= 0x80;
                    }
                }
            }
        }

        via_6522_registers[n][T1C_L] = t1 & 0xff;
        via_6522_registers[n][T1C_H] = (t1 >> 8) & 0xff;

        if (t1_timed_out)
        {
            via_6522_registers[n][IFR]  |=  0x40;
        }

        /* Handle Timer 2 */
        t2 = via_6522_registers[n][T2C_L] + ((uint16_t)via_6522_registers[n][T2C_H] << 8);

        if (via_6522_registers[n][ACR] & (1 << 5)) /* Free running */
        {
            t2 -= cpu_tick_count;
        }

        if (t2 < 0)
        {
            t2 &= 0xffff;
            via_6522_registers[n][IFR]  |=  0x20;
        }

        via_6522_registers[n][T2C_L] = t2 & 0xff;
        via_6522_registers[n][T2C_H] = (t2 >> 8) & 0xff;

        /* Set the IRQ line if any interrupts are enabled and set */
        if ((via_6522_registers[n][IER] & 0x7F) & (via_6522_registers[n][IFR] & 0x7f))
        {
            via_6522_registers[n][IFR]  |=  0x80;
            rv = true;
        }
        else
        {
            via_6522_registers[n][IFR]  &=  0x7f;
        }
    }

    return rv;
}

void via_6522_update_io_regs()
{
    for (int device_index = 0; device_index < number_of_devices; device_index++)
    {
        for (int port_index = 0; port_index < 2; port_index++)
        {
            via_6522_registers[device_index][port_index] = (~via_6522_registers[device_index][2 + port_index]) & port_in[device_index][port_index];
        }
    }
}


void via_6522_set_input_port(int device_index, int port_index, via_6522_port_operation_t operation, uint16_t port_value)
{
    port_index &= 1;

    switch (operation)
    {
    case via_6522_set:
        port_in[device_index][port_index] |= port_value;
        break;

    case via_6522_clear:
        port_in[device_index][port_index] &= ~port_value;
        break;

    case via_6522_write_all:
        port_in[device_index][port_index] = port_value;
        break;
    }

    via_6522_update_io_regs();
}


uint8_t via_6522_read_register(int device_index, int register_index)
{
    if ((register_index < 0x00) || (register_index > 0x0f))
    {
        return 0x00;
    }

    uint8_t rv;
    via_6522_registers[device_index][IFR] &= flag_clear_read_table[register_index];
    return via_6522_registers[device_index][register_index];
}


void via_6522_write_register(int device_index, int register_index, uint8_t register_value)
{
    if ((register_index < 0x00) || (register_index > 0x0f))
    {
        return;
    }

    via_6522_registers[device_index][IFR] &= flag_clear_write_table[register_index];
// printf("%04X via_6522_write_register(%d, %d, %02x)\r\n", cpu_6502_get_pc(), device_index, register_index, register_value);

    switch (register_index)
    {
    case T1C_H:
        via_6522_registers[device_index][T1L_H] = register_value;
        via_6522_registers[device_index][T1C_L] = via_6522_registers[device_index][T1L_L];
        break;

    case ORA:
        port_out[device_index][0] = (register_value & ~via_6522_registers[device_index][DDRA]) |
                                    (port_in[device_index][0] & via_6522_registers[device_index][DDRA]);
        via_6522_update_io_regs();
        break;

    case ORB:
        port_out[device_index][1] = (register_value & ~via_6522_registers[device_index][DDRB]) |
                                    (port_in[device_index][1] & via_6522_registers[device_index][DDRB]);
        via_6522_update_io_regs();
        break;

    case IER:
        if (register_value & 0x80)
        {
            via_6522_registers[device_index][register_index] |= (register_value & 0x7f);
        }
        else
        {
            via_6522_registers[device_index][register_index] &= ~(register_value & 0x7f);
        }

        break;

    default:
        via_6522_registers[device_index][register_index] = register_value;
        break;
    }
}


uint8_t via_6522_read_callback(uint16_t address)
{
    uint8_t rv = 0;

    for (int i = 0; i < number_of_devices; i++)
    {
        if ((address >= address_table[i]) && (address <= (address_table[i] + 15)))
        {
            rv = via_6522_read_register(i, address - address_table[i]);

            if ((address & 0x0f) < 4)
            {
                address_print(address);
            }
        }
    }

    return rv;
}

void via_6522_write_callback(uint16_t address, uint8_t value)
{
    for (int i = 0; i < number_of_devices; i++)
    {
        if ((address >= address_table[i]) && (address <= (address_table[i] + 15)))
        {
            via_6522_write_register(i, address - address_table[i], value);

            if ((address & 0x0f) < 4)
            {
                address_print(address);
            }
        }
    }
}

void via_6522_reload()
{
    uint8_t *memory;

    for (int i = 0; i < number_of_devices; i++)
    {
        memory = system_get_memory_pointer(address_table[i]);

        for (int reg = 0; reg < 16; reg++)
        {
            system_write_memory(address_table[i] + reg, *memory++);
        }
    }
}

void via_6522_reset(uint8_t bank, uint16_t address)
{
    int n;

    for (n = 0; n < number_of_devices; n++)
    {
        if (address_table[n] == address)
        {
            memset(via_6522_registers[n], 0xff, 16);
            via_6522_registers[n][DDRA] = 0x00;
            via_6522_registers[n][DDRB] = 0x00;
            via_6522_registers[n][IER] = 0;
            via_6522_registers[n][IFR] = 0;
        }
    }
}


int via_6522_initialise(uint8_t bank, uint16_t address, uint16_t param, char *identifier)
{
    if (number_of_devices == MAX_DEVICES)
    {
        return RV_DEVICE_NOT_ADDED;
    }

    address_table[number_of_devices] = address;
    port_out[number_of_devices][0] = 0;
    port_out[number_of_devices][1] = 0;
    via_6522_set_input_port(number_of_devices, 0, via_6522_write_all, 0xff);
    via_6522_set_input_port(number_of_devices, 1, via_6522_write_all, 0xff);
    system_register_memory_mapped_device(address, address + 15, via_6522_read_callback, via_6522_write_callback, false);
    number_of_devices++;
    return RV_OK;
}

