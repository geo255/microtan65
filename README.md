# microtan65

[](https://github.com/geo255/microtan65#microtan65)

Tangerine Microtan 65 emulator

Emulates a Microtan 65 system with the following specifications:

- Microtan 65 system board with chunky graphics and lowercase character options
- Tanex with 7k RAM, 2x6522, XBUG and BASIC EPROMS (no serial I/O yet)
- Sound board - 2x AY8910 plus "Space Invaders" sounds
- 64k RAM board
- Hex keypad
- ASCII keyboard
- 8-way Joystick (via cursor keys) connected to 6522
- 4x Tangerine Hi_res boards, providing RGBI at 256x256
- Prototype "GPU" graphics board - in development
- My .m65 snapshot format and Intel HEX program loading

## TODO:

[](https://github.com/geo255/microtan65#todo)

- Complete hex keypad emulation
- Emulation of the serial interface
- Support for more file formats (Zillion hex)
- A menu
- Finish the GPU

## BUILDING:

[](https://github.com/geo255/microtan65#building)

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

[](https://github.com/geo255/microtan65#running)

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

The Microtan TANBUG and BASIC are case sensitive and commands are all upper case, so the emulator swaps lower case and upper case, so you don't need to press CAPS yourself.

The numeric keypad "ENTER" key is used as the "LINEFEED" key on the Microtan keyboard.

F1 displays a brief help window F2 emulates the Tangerine Hex Keypad F3 emulated the ASCII keyboard F5 resets the CPU

## GAME KEYS:

[](https://github.com/geo255/microtan65#game-keys)

### Adventure

[](https://github.com/geo255/microtan65#adventure)

- Text input

### Astro Fighter

[](https://github.com/geo255/microtan65#astro-fighter)

- Space: Fire
- Cursor keys: Move

### Berzerk

[](https://github.com/geo255/microtan65#berzerk)

- Space: Fire
- Cursor keys: Move

### Defender

[](https://github.com/geo255/microtan65#defender)

- Left ctrl: Thrust
- Space: Fire
- Cursor up/down: Move
- Cursor Right: Reverse
- Cursor Left: Smart bomb

### Gobbler

[](https://github.com/geo255/microtan65#gobbler)

- Not yet working: hex keypad not fully emulated

### Hangman

[](https://github.com/geo255/microtan65#hangman)

- Text input

### Hells Bells

[](https://github.com/geo255/microtan65#hells-bells)

- Left ctrl: Jump
- Cursor left/right: Move

### Invaders

[](https://github.com/geo255/microtan65#invaders)

- Left shift: Start game
- Space: Fire
- Cursor keys: Move

### Lunar Lander

[](https://github.com/geo255/microtan65#lunar-lander)

- Text input

### Moon Cresta

[](https://github.com/geo255/microtan65#moon-cresta)

- Left ctrl: Start
- Space: Fire
- Cursor keys: Move

### Moon Rescue

[](https://github.com/geo255/microtan65#moon-rescue)

- Left ctrl: Start/Launch lunar module
- Space: Thrust/Fire
- Cursor keys: Move

### Othello

[](https://github.com/geo255/microtan65#othello)

- Text input

### Slot Machine

[](https://github.com/geo255/microtan65#slot-machine)

- Space: Start
- 1..3: Nudge/hold reel

### Space Rocks

[](https://github.com/geo255/microtan65#space-rocks)

- Not yet working: hex keypad not fully emulated

## FURTHER INFORMATION:

[](https://github.com/geo255/microtan65#further-information)

Go to my website at [https://geoff.org.uk/microtan/](https://geoff.org.uk/microtan/) for more Microtan 65 information and documentation.


To load an Intel HEX file:

```
./microtan65 program.hex
```

The emulator auto-detects `.m65`, `.hex`, `.ihx`, and `.ihex` files by extension (and falls back to content detection for Intel HEX).
