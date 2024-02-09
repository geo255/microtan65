#include <SDL.h>
#include <stdbool.h>
#include "cpu_6502.h"
#include "function_return_codes.h"
#include "system.h"

// There are 4 keyboard I/O registers, which are addressed 0xBFF0 - 0xBFF3,
// but bits 2 and 3 of the address bus are not connected, so the registers
// are mirrored at 0xBFF4 - 0xBFFF
static uint8_t keyboard_regs[4];
static bool use_hex_keypad = false;
static uint8_t hex_keypad[4];

void keyboard_write_callback(uint16_t address, uint8_t value)
{
    keyboard_regs[address & 0x03] = value;
}

uint8_t keyboard_read_callback(uint16_t address)
{
    if (((address & 0x03) == 0x03) && (use_hex_keypad))
    {
        uint8_t rv = 0;

        for (int column = 0; column < 4; column++)
        {
            if (keyboard_regs[2] & (1 << column))
            {
                rv |= hex_keypad[column];
            }
        }

        return rv;
    }
    else
    {
        return keyboard_regs[address & 0x03];
    }
}

void keyboard_keypress(uint8_t keypress)
{
    // ASCII keyboard keys appear at address 0xBFF3
    // Set the port direcly, so not to interfere with the chunky graphics
    if (!use_hex_keypad)
    {
        keyboard_regs[3] = keypress | 0x80;
        cpu_6502_assert_irq();
    }
}

void keyboard_keypad_key(int row, int column, bool key_down)
{
    if ((column < 0) || (column > 3))
    {
        return;
    }

    if (key_down)
    {
        hex_keypad[column] |= (1 << row);
    }
    else
    {
        hex_keypad[column] &= ~(1 << row);
    }
}

void keyboard_use_hex_keypad(bool option)
{
    use_hex_keypad = option;
}

bool keyboard_using_hex_keypad()
{
    return use_hex_keypad;
}

int keyboard_initialise(uint8_t bank, uint16_t address, uint16_t param, char *identifier)
{
    memset(hex_keypad, 0x00, sizeof(hex_keypad));
    use_hex_keypad = false;
    system_register_memory_mapped_device(address, address + 0x0f, keyboard_read_callback, keyboard_write_callback, false);
    return RV_OK;
}

