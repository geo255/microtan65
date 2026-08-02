.RECIPEPREFIX := >

CC ?= gcc
SRC_DIR := src
BUILD_DIR := build
TARGET := $(BUILD_DIR)/microtan65
PYTHON ?= python3

SOURCES := $(wildcard $(SRC_DIR)/*.c)
HEADERS := $(wildcard $(SRC_DIR)/*.h)
OBJECTS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SOURCES))

BASE_CFLAGS := $(shell sdl2-config --cflags) -std=c11 -D_POSIX_C_SOURCE=200809L -I$(SRC_DIR)
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

$(TARGET): $(OBJECTS) | $(BUILD_DIR)
>$(CC) $(OBJECTS) $(LDFLAGS) $(LDLIBS) -o $(TARGET)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(HEADERS) | $(BUILD_DIR)
>$(CC) $(CFLAGS) -c $< -o $@

run: $(TARGET)
>./$(TARGET)

$(BUILD_DIR):
>mkdir -p $(BUILD_DIR)

smoke:
>missing=0; \
>for f in \
>  assets/microtan.rom \
>  assets/tandos.rom \
>  assets/charset.rom \
>  assets/saa5050.chr \
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

test-tandos: | $(BUILD_DIR)
>$(CC) $(BASE_CFLAGS) $(WARN_CFLAGS) $(DEBUG_CFLAGS) \
>  tests/tandos_test.c src/tandos.c -o $(BUILD_DIR)/tandos_test
>./$(BUILD_DIR)/tandos_test

test-rtc: | $(BUILD_DIR)
>$(CC) $(BASE_CFLAGS) $(WARN_CFLAGS) $(DEBUG_CFLAGS) \
>  tests/rtc_test.c src/rtc.c -o $(BUILD_DIR)/rtc_test
>./$(BUILD_DIR)/rtc_test

test-keyboard: | $(BUILD_DIR)
>$(CC) $(BASE_CFLAGS) $(WARN_CFLAGS) $(DEBUG_CFLAGS) \
>  tests/keyboard_test.c src/keyboard.c -o $(BUILD_DIR)/keyboard_test
>./$(BUILD_DIR)/keyboard_test

format:
>clang-format -i $(SOURCES) $(HEADERS)

lint:
>clang-format --dry-run --Werror $(SOURCES) $(HEADERS)

clean:
>$(RM) $(OBJECTS) $(TARGET) $(TARGET).exe

.PHONY: all release debug sanitize run smoke test-tandos test-rtc test-keyboard format lint clean





