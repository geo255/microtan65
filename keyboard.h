#ifndef __KEYBOARD_H__
#define __KEYBOARD_H__

#include "system.h"

extern void keyboard_keypress(uint8_t keypress);
extern void keyboard_use_hex_keypad(bool option);
extern bool keyboard_using_hex_keypad();
extern void keyboard_keypad_key(int row, int column, bool key_down);
extern int keyboard_initialise(uint8_t bank, uint16_t address, uint16_t param, char *identifier);

#endif // __KEYBOARD_H__
