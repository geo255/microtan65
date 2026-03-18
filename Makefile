.RECIPEPREFIX := >

CC ?= gcc
TARGET := microtan65
PYTHON ?= python3

SOURCES := ay8910.c cpu_6502.c display.c eprom.c invaders_sound.c joystick.c keyboard.c main.c popup.c serial.c system.c via_6522.c
HEADERS := ay8910.h cpu_6502.h display.h eprom.h external_filenames.h function_return_codes.h invaders_sound.h joystick.h keyboard.h popup.h serial.h system.h via_6522.h
OBJECTS := $(SOURCES:.c=.o)

BASE_CFLAGS := $(shell sdl2-config --cflags) -std=c11 -D_POSIX_C_SOURCE=200809L
WARN_CFLAGS := -Wall -Wextra -Wpedantic
DEBUG_CFLAGS := -O0 -g3
RELEASE_CFLAGS := -O2
SANITIZE_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer

LDLIBS := $(shell sdl2-config --libs) -lSDL2_ttf -lSDL2_mixer
LDFLAGS ?=
CFLAGS ?= $(BASE_CFLAGS) $(WARN_CFLAGS) $(RELEASE_CFLAGS)

all: release

release: CFLAGS := $(BASE_CFLAGS) $(WARN_CFLAGS) $(RELEASE_CFLAGS)
release: $(TARGET)

debug: CFLAGS := $(BASE_CFLAGS) $(WARN_CFLAGS) $(DEBUG_CFLAGS)
debug: $(TARGET)

sanitize: CFLAGS := $(BASE_CFLAGS) $(WARN_CFLAGS) $(DEBUG_CFLAGS) $(SANITIZE_FLAGS)
sanitize: LDFLAGS := $(SANITIZE_FLAGS)
sanitize: $(TARGET)

$(TARGET): $(OBJECTS)
>$(CC) $(OBJECTS) $(LDFLAGS) $(LDLIBS) -o $(TARGET)

%.o: %.c $(HEADERS)
>$(CC) $(CFLAGS) -c $< -o $@

run: $(TARGET)
>./$(TARGET)

smoke:
>missing=0; \
>for f in \
>  assets/microtan.rom \
>  assets/charset.rom \
>  assets/fonts/arial.ttx \
>  assets/fonts/cour.ttf \
>  assets/sounds/saucer.wav \
>  programs/adventure.m65 \
>  programs/astrofighter.m65 \
>  programs/berzerk.m65 \
>  programs/defender.m65 \
>  programs/invaders.m65; do \
>  if [ ! -f "$$f" ]; then \
>    echo "Missing: $$f"; \
>    missing=1; \
>  fi; \
>done; \
>if [ $$missing -ne 0 ]; then \
>  echo "Smoke check failed."; \
>  exit 1; \
>fi; \
>echo "Smoke check passed."

format:
>clang-format -i $(SOURCES) $(HEADERS)

lint:
>clang-format --dry-run --Werror $(SOURCES) $(HEADERS)

clean:
>$(RM) $(OBJECTS) $(TARGET) $(TARGET).exe

.PHONY: all release debug sanitize run smoke format lint clean





