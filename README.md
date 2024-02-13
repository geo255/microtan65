# microtan65
Tangerine Microtan 65 emulator

Emulates a Microtan 65 system with the following specifications:
* Microtan 65 system board with chunky graphics and lowercase character options
* Tanex with 7k RAM, 2x6522, XBUG and BASIC EPROMS (no serial I/O yet)
* 64k RAM board
* Hex keypad
* ASCII keyboard
* 8-way Joystick (via cursor keys) connected to 6522
* 4x Tangerine Hi_res boards, providing RGBI at 256x256
* Prototype "GPU" graphics board - in development
* My .m65 file format

## TODO:
* Complete hex keypad emulation
* Emulation of the 2xAY8910 soundboard
* Emulation of the serial interface
* Support for more file formats (Zillion hex and Intel hex)
* A menu
* Finish the GPU

## BUILDING:
Install libraries (Ubuntu):
```
sudo apt update
sudo apt upgrade
sudo apt install gcc make
sudo apt install libsdl2-dev libsdl2-2.0-0 libsdl2-image-dev libsdl2-mixer-dev libsdl2-ttf-dev
```

Build:
```
make clean
make
```

## RUNNING:
```
./microtan65
```
To start with a program loaded into memory:
```
./microtan65 <file name>
```
eg.
```
./microtan65 defender.m65
```
The Microtan TANBUG and BASIC are case sensitive and commands are all upper case,
so the emulator swaps lower case and upper case, so you don't need to press
CAPS yourself.

The numeric keypad "ENTER" key is used as the "LINEFEED" key on the Microtan keyboard.

F1 displays a brief help window
F2 emulates the Tangerine Hex Keypad
F3 emulated the ASCII keyboard
F5 resets the CPU

## GAME KEYS:
### Adventure
* Text input
### Astro Fighter
* Space: Fire
* Cursor keys: Move
### Berzerk
* Space: Fire
* Cursor keys: Move
### Defender
* Left ctrl: Thrust
* Space: Fire
* Cursor up/down: Move
* Cursor Right: Reverse
* Cursor Left: Smart bomb
### Gobbler
* Not yet working: hex keypad not fully emulated
### Hangman
* Text input
### Hells Bells
* Left ctrl: Jump
* Cursor left/right: Move
### Invaders
* Left shift: Start game
* Space: Fire
* Cursor keys: Move
### Lunar Lander
* Text input
### Moon Cresta
* Left ctrl: Start
* Space: Fire
* Cursor keys: Move
### Moon Rescue
* Left ctrl: Start/Launch lunar module
* Space: Thrust/Fire
* Cursor keys: Move
### Othello
* Text input
### Slot Machine
* Space: Start
* 1..3: Nudge/hold reel
### Space Rocks
* Not yet working: hex keypad not fully emulated
## FURTHER INFORMATION:
Go to my website at https://geoff.org.uk/microtan/ for more Microtan 65 information
and documentation.
