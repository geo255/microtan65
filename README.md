# microtan65
Tangerine Microtan 65 emulator

Emulates the a Microtan 65 system with the following spec:
* Microtan 65 system board with chunky graphics and lower case character options
* Tanex with 7k RAM, 2x6522, XBUG and BASIC EPROMS (no serial I/O yet)
* Hex keypad
* ASCII keyboard
* 8 way Joystick (via cursor keys) connected to 6522
* 4x Tangerine Hi_res boards, providing RGBI at 256x256
* Prototype "GPU" graphics board - in development
* My .m65 file format

## TODO:
* Emulation of the 2xAY8910 sound board
* Support for more file formats (Zillion hex and Intel hex)
* A menu
* Finish the GPU

## BUILDING:
Install libraries:
'sudo apt install libsdl2-dev libsdl2-2.0-0'
'sudo apt install libsdl2-image-dev libsdl2-mixer-dev'

Build:
'make clean'
'make'

## RUNNING:
./microtan65

or

./microtan65 <file name>

eg.
./microtan65 defender.m65

The Microtan TANBUG and BASIC are case sensitive and commands are all upper case,
so the emulator swaps lower case and upper case, so you don't need to press
CAPS yourself.

The numeric keypad "ENTER" key is used as the "LINEFEED" key on the Microtan keyboard.

F1 displays a brief help window

## FURTHER INFORMATION:
Go to my website at https://geoff.org.uk/microtan/ for more Microtan 65 information
and documentation.


