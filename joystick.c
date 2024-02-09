#include <SDL.h>
#include <stdint.h>
#include <stdbool.h>
#include "keyboard.h"
#include "via_6522.h"

typedef struct joystick_definition_t
{
    // SDL scancode
    uint16_t sdl_scancode;

    // Data for using a joystick plugged into a 6522 when the ASCII keyboard is selected
    int via_6522_index;
    int port;
    uint8_t bit;

    // Data for using a key heypad "joystick"
    int row;
    int column;
} joystick_definition_t, *joystick_definition_ptr_t;

joystick_definition_t joystick_definition_table[] =
{
    { SDL_SCANCODE_LSHIFT, 0, 1, 1, 0, 2 },
    { SDL_SCANCODE_SPACE,  0, 1, 2, 4, 0 },
    { SDL_SCANCODE_LCTRL,   0, 1, 3, 2, 0 },
    { SDL_SCANCODE_UP,     0, 1, 5, 1, 2 },
    { SDL_SCANCODE_DOWN,   0, 1, 7, 1, 0 },
    { SDL_SCANCODE_LEFT,   0, 1, 4, 2, 1 },
    { SDL_SCANCODE_RIGHT,  0, 1, 6, 0, 1 }
};

void joystick()
{
    static uint8_t previous_joystick_keys = 0;
    uint8_t joystick_keys = 0;
    const uint8_t *key_state = SDL_GetKeyboardState(NULL);

    for (int i = 0; i < sizeof(joystick_definition_table) / sizeof(joystick_definition_table[0]); i++)
    {
        if (key_state[joystick_definition_table[i].sdl_scancode])
        {
            joystick_keys |= (1 << i);
        }
    }

    if (joystick_keys == previous_joystick_keys)
    {
        return;
    }

    previous_joystick_keys = joystick_keys;

    for (int i = 0; i < sizeof(joystick_definition_table) / sizeof(joystick_definition_table[0]); i++)
    {
        bool key_down = (joystick_keys >> i) & 0x01;

        if (keyboard_using_hex_keypad())
        {
            keyboard_keypad_key(joystick_definition_table[i].row, joystick_definition_table[i].column, key_down);
        }
        else
        {
            via_6522_set_input_port(
                joystick_definition_table[i].via_6522_index, joystick_definition_table[i].port,
                key_down ? via_6522_clear : via_6522_set, 1 << joystick_definition_table[i].bit
            );
        }
    }
}


