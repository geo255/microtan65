CC = gcc
CFLAGS = $(shell sdl2-config --cflags)
LIBS = $(shell sdl2-config --libs) -lSDL2_ttf

all: output

output: main.c
	$(CC) $(CFLAGS) cpu_6502.c display.c eprom.c joystick.c keyboard.c main.c popup.c serial.c system.c via_6522.c $(LIBS) -o microtan65

clean:
	rm -f output
