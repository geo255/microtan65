#define _POSIX_C_SOURCE 200809L
#include <SDL.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "cpu_6502.h"
#include "display.h"
#include "eprom.h"
#include "function_return_codes.h"
#include "invaders_sound.h"
#include "joystick.h"
#include "keyboard.h"
#include "popup.h"
#include "system.h"
#include "via_6522.h"

#define MICROTAN_DEFAULT_CLOCK_FREQUENCY 750000
#define LOOP_EXECUTE_TIME_MS             20

const char* SETTINGS_FILE = "microtan_settings.txt";

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static bool directory_exists(const char* path) {
  struct stat st;
  return path && (*path != '\0') && (stat(path, &st) == 0) && S_ISDIR(st.st_mode);
}

static void trim_trailing_newline(char* text) {
  if (!text) {
    return;
  }

  size_t length = strlen(text);
  while (length > 0) {
    char ch = text[length - 1];
    if ((ch == '\n') || (ch == '\r')) {
      text[length - 1] = '\0';
      length--;
    } else {
      break;
    }
  }
}

static void choose_default_file_directory(char* output, size_t output_size) {
  if (directory_exists("programs")) {
    snprintf(output, output_size, "programs");
  } else {
    snprintf(output, output_size, ".");
  }
}

static void update_file_dialog_directory(const char* selected_file, char* output, size_t output_size) {
  if (!selected_file || !output || (output_size == 0)) {
    return;
  }

  const char* slash = strrchr(selected_file, '/');
  const char* backslash = strrchr(selected_file, '\\');
  const char* separator = slash;

  if (backslash && (!separator || (backslash > separator))) {
    separator = backslash;
  }

  if (!separator) {
    if (directory_exists(".")) {
      snprintf(output, output_size, ".");
    }
    return;
  }

  size_t directory_length = (size_t)(separator - selected_file);
  if (directory_length == 0) {
    snprintf(output, output_size, "/");
    return;
  }

  if (directory_length >= output_size) {
    directory_length = output_size - 1;
  }

  memcpy(output, selected_file, directory_length);
  output[directory_length] = '\0';
}

void save_window_settings(SDL_Window* window, const char* file_dialog_directory) {
  int x, y, width, height;
  SDL_GetWindowSize(window, &width, &height);
  SDL_GetWindowPosition(window, &x, &y);
  FILE* file = fopen(SETTINGS_FILE, "w");

  if (file) {
    const char* save_directory = (file_dialog_directory && (*file_dialog_directory != '\0')) ? file_dialog_directory : ".";
    fprintf(file, "%d %d %d %d %d\n%s\n", x, y, width, height, (int)display_get_hires_mode(), save_directory);
    fclose(file);
  }
}

void load_window_settings(int* x, int* y, int* width, int* height, display_hires_mode_t* display_mode, char* file_dialog_directory, size_t file_dialog_directory_size) {
  FILE* file = fopen(SETTINGS_FILE, "r");
  char path_line[PATH_MAX];
  *display_mode = DISPLAY_HIRES_MODE_NONE;
  choose_default_file_directory(file_dialog_directory, file_dialog_directory_size);

  if (file) {
    int display_mode_raw = 0;
    int values_read = fscanf(file, "%d %d %d %d %d", x, y, width, height, &display_mode_raw);

    if (values_read < 4) {
      *x = SDL_WINDOWPOS_CENTERED;
      *y = SDL_WINDOWPOS_CENTERED;
    } else if ((values_read >= 5) && (display_mode_raw >= (int)DISPLAY_HIRES_MODE_NONE) && (display_mode_raw <= (int)DISPLAY_HIRES_MODE_EXTENDED)) {
      *display_mode = (display_hires_mode_t)display_mode_raw;
    }

    // Consume remainder of line with geometry/mode fields.
    if (fgets(path_line, sizeof(path_line), file) == NULL) {
      path_line[0] = '\0';
    }

    // Optional persisted file-dialog directory line.
    if (fgets(path_line, sizeof(path_line), file) != NULL) {
      trim_trailing_newline(path_line);
      if ((path_line[0] != '\0') && directory_exists(path_line)) {
        snprintf(file_dialog_directory, file_dialog_directory_size, "%s", path_line);
      }
    }

    fclose(file);
  }
}

void set_pixel(SDL_Surface* surface, int x, int y, Uint32 pixel) {
  Uint8* target_pixel = (Uint8*)surface->pixels + y * surface->pitch + x * 4;
  *(Uint32*)target_pixel = pixel;
}

SDL_Texture* create_scanline_texture(SDL_Renderer* renderer, int width, int height) {
  int scan_width = 1;
  int scan_height = 4;
  int texture_width = width * scan_width;
  int texture_height = height * scan_height;
  SDL_Surface* scanline_surface = SDL_CreateRGBSurface(0, texture_width, texture_height, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);

  if (!scanline_surface) {
    return NULL;
  }

  uint32_t transparent_100 = SDL_MapRGBA(scanline_surface->format, 0, 0, 0, 0);
  uint32_t transparent_50 = SDL_MapRGBA(scanline_surface->format, 0, 0, 0, 128);
  uint32_t transparent_25 = SDL_MapRGBA(scanline_surface->format, 0, 0, 0, 192);

  for (int y = 0; y < texture_height; y += scan_height) {
    for (int x = 0; x < texture_width; x++) {
      set_pixel(scanline_surface, x, y, transparent_100);
      set_pixel(scanline_surface, x, y + 1, transparent_100);
      set_pixel(scanline_surface, x, y + 2, transparent_50);
      set_pixel(scanline_surface, x, y + 3, transparent_25);
    }
  }

  SDL_Texture* scanline_texture = SDL_CreateTextureFromSurface(renderer, scanline_surface);
  SDL_FreeSurface(scanline_surface);
  return scanline_texture;
}


static bool parse_hex_u16(const char* text, uint16_t* value) {
  while (isspace((unsigned char)*text)) {
    text++;
  }

  if (*text == '$') {
    text++;
  } else if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
    text += 2;
  }

  if (*text == '\0') {
    return false;
  }

  errno = 0;
  char* end = NULL;
  unsigned long parsed = strtoul(text, &end, 16);

  while (end && isspace((unsigned char)*end)) {
    end++;
  }

  if (errno != 0 || !end || *end != '\0' || parsed > 0xFFFFUL) {
    return false;
  }

  *value = (uint16_t)parsed;
  return true;
}

static bool parse_hex_range(const char* text, uint16_t* start, uint16_t* end) {
  char buffer[128];
  snprintf(buffer, sizeof(buffer), "%s", text);

  char* separator = strchr(buffer, '-');
  if (!separator) {
    separator = strchr(buffer, ':');
  }

  if (!separator) {
    return false;
  }

  *separator = '\0';
  separator++;

  uint16_t start_value;
  uint16_t end_value;

  if (!parse_hex_u16(buffer, &start_value) || !parse_hex_u16(separator, &end_value)) {
    return false;
  }

  if (start_value > end_value) {
    return false;
  }

  *start = start_value;
  *end = end_value;
  return true;
}

static const char* display_mode_name(display_hires_mode_t mode) {
  switch (mode) {
    case DISPLAY_HIRES_MODE_TANGERINE:
      return "Tangerine hi-res (RGBI)";
    case DISPLAY_HIRES_MODE_EXTENDED:
      return "GPU";
    case DISPLAY_HIRES_MODE_NONE:
    default:
      return "Text/chunky";
  }
}

static void show_main_menu(SDL_Renderer* renderer, bool* display_overwritten, char* file_dialog_directory, size_t file_dialog_directory_size) {
  const char* menu_items[] = {
    "Reset system",
    "Select hex keypad input",
    "Select ASCII keyboard input",
    "Load program file (.m65/.hex/.ihx/.ihex)",
    "Save snapshot (.m65)",
    "Save Intel HEX address range",
    "Display options",
    "Help",
    "Cancel"};

  int selection = popup_menu_select(renderer, "Microtan Menu", menu_items, 9, 0);
  char file_name[PATH_MAX];
  char range_input[128];
  int rv;

  switch (selection) {
    case 0:
      system_reset();
      popup_show(renderer, "System reset.");
      break;

    case 1:
      keyboard_use_hex_keypad(true);
      popup_show(renderer, "Hex keypad input selected.");
      break;

    case 2:
      keyboard_use_hex_keypad(false);
      popup_show(renderer, "ASCII keyboard input selected.");
      break;

    case 3: {
      const char* load_extensions[] = {".m65", ".hex", ".ihx", ".ihex"};

      if (popup_file_select(renderer, "Load Program", file_dialog_directory, load_extensions, 4, false, "", file_name, sizeof(file_name))) {
        update_file_dialog_directory(file_name, file_dialog_directory, file_dialog_directory_size);
        // Match startup load behavior: reset hardware state before loading a program file.
        system_reset();
        rv = system_load_program_file(file_name);
        if ((rv == RV_OK) && (strstr(file_name, "berzerk") != NULL)) {
          keyboard_use_hex_keypad(true);
        }
        if (rv == RV_OK) {
          popup_show(renderer, "Program loaded.");
        } else {
          popup_show(renderer, "Load failed. See terminal output for details.");
        }
      }
      break;
    }

    case 4: {
      const char* m65_extensions[] = {".m65"};

      if (popup_file_select(renderer, "Save Snapshot", file_dialog_directory, m65_extensions, 1, true, "snapshot.m65", file_name, sizeof(file_name))) {
        update_file_dialog_directory(file_name, file_dialog_directory, file_dialog_directory_size);
        rv = system_save_m65_file(file_name);
        if (rv == RV_OK) {
          popup_show(renderer, "Snapshot saved.");
        } else {
          popup_show(renderer, "Save failed. See terminal output for details.");
        }
      }
      break;
    }

    case 5: {
      uint16_t start_address;
      uint16_t end_address;
      const char* hex_extensions[] = {".hex", ".ihx", ".ihex"};

      if (!popup_file_select(renderer, "Save Intel HEX", file_dialog_directory, hex_extensions, 3, true, "range.hex", file_name, sizeof(file_name))) {
        break;
      }
      update_file_dialog_directory(file_name, file_dialog_directory, file_dialog_directory_size);

      if (!popup_prompt_input(renderer, "Save Intel HEX", "Address range start-end (hex)", "0200-03FF", range_input, sizeof(range_input))) {
        break;
      }

      if (!parse_hex_range(range_input, &start_address, &end_address)) {
        popup_show(renderer, "Invalid range. Example: 0200-03FF");
        break;
      }

      rv = system_save_intel_hex_range(file_name, start_address, end_address);
      if (rv == RV_OK) {
        popup_show(renderer, "Intel HEX saved.");
      } else {
        popup_show(renderer, "Save failed. See terminal output for details.");
      }
      break;
    }
    case 6: {
      const char* display_items[] = {
        "Text/chunky only",
        "Tangerine hi-res (RGBI)",
        "GPU display mode",
        "Cancel"};
      int current_mode = (int)display_get_hires_mode();
      if (current_mode < 0 || current_mode > 2) {
        current_mode = 0;
      }

      int display_selection = popup_menu_select(renderer, "Display Options", display_items, 4, current_mode);
      if (display_selection >= 0 && display_selection <= 2) {
        display_set_hires_mode((display_hires_mode_t)display_selection);
        char message[96];
        snprintf(message, sizeof(message), "Display mode: %s", display_mode_name(display_get_hires_mode()));
        popup_show(renderer, message);
      }
      break;
    }

    case 7:
      popup_show(renderer,
                 "F1: Open menu\n"
                 "Menu has reset/input/load/save actions\n"
                 "Menu -> Display options: Text/Tangerine/GPU\n"
                 "F2: Select hex keypad input\n"
                 "F3: Select ASCII keyboard input\n"
                 "F5: Reset system\n");
      break;

    default:
      break;
  }

  *display_overwritten = true;
}

int main(int argc, char* argv[]) {
  if (system_initialise() != RV_OK) {
    return 0;
  }

  system_reset();

  if (argc > 1) {
    if (system_load_program_file(argv[1]) != RV_OK) {
      printf("Failed to load [%s]\r\n", argv[1]);
    }

    if (strstr(argv[1], "berzerk") != NULL) {
      keyboard_use_hex_keypad(true);
    }
  }
  srand(time(NULL));

  SDL_Init(SDL_INIT_VIDEO);
  int x = SDL_WINDOWPOS_CENTERED, y = SDL_WINDOWPOS_CENTERED, width = DISPLAY_WIDTH, height = DISPLAY_HEIGHT;
  display_hires_mode_t saved_display_mode = DISPLAY_HIRES_MODE_NONE;
  char file_dialog_directory[PATH_MAX];
  load_window_settings(&x, &y, &width, &height, &saved_display_mode, file_dialog_directory, sizeof(file_dialog_directory));
  display_set_hires_mode(saved_display_mode);
  SDL_Window* window = SDL_CreateWindow("Microtan 65", x, y, width, height, SDL_WINDOW_RESIZABLE);
  SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
  SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, DISPLAY_WIDTH, DISPLAY_HEIGHT);
  SDL_Texture* scanlines = create_scanline_texture(renderer, DISPLAY_WIDTH, DISPLAY_HEIGHT);
  uint32_t pixels[DISPLAY_WIDTH * DISPLAY_HEIGHT];
  bool is_running = true;
  SDL_Event event;
  struct timespec start_time;
  struct timespec end_time;
  struct timespec sleep_time;
  bool display_overwritten = false;

  while (is_running) {
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    // Execute LOOP_EXECUTE_TIME_MS's worth of instructions
    cpu_6502_execute(MICROTAN_DEFAULT_CLOCK_FREQUENCY * LOOP_EXECUTE_TIME_MS / 1000);

    // If the display has been updated, re-render the window
    if ((display_updated_event()) || (display_overwritten)) {
      display_overwritten = false;
      display_render(pixels);
      SDL_UpdateTexture(texture, NULL, pixels, DISPLAY_WIDTH * sizeof(Uint32));
      SDL_RenderClear(renderer);
      SDL_GetWindowSize(window, &width, &height);
      SDL_Rect dest_rect = {0, 0, width, height};
      SDL_RenderCopy(renderer, texture, NULL, &dest_rect);
      SDL_RenderCopy(renderer, scanlines, NULL, NULL);
      SDL_RenderPresent(renderer);
    }

    while (SDL_PollEvent(&event)) {
      switch (event.type) {
        case SDL_QUIT:
          is_running = false;
          break;

        case SDL_WINDOWEVENT:
          switch (event.window.event) {
            case SDL_WINDOWEVENT_RESIZED: {
              int new_width = event.window.data1;
              int new_height = event.window.data2 & 0xffffff00;

              if (new_height < 512) {
                new_height = 512;
              }

              // Calculate desired height for 5:4 aspect ratio
              int desired_width = (5 * new_height) / 4;

              if (new_width != desired_width) {
                SDL_SetWindowSize(window, desired_width, new_height);
              }

              SDL_RenderClear(renderer);
              int width, height;
              SDL_GetWindowSize(window, &width, &height);
              SDL_Rect dest_rect = {0, 0, width, height};
              SDL_RenderCopy(renderer, texture, NULL, &dest_rect);
              SDL_RenderCopy(renderer, scanlines, NULL, NULL);
              SDL_RenderPresent(renderer);
              break;
            }
          }

          break;

        case SDL_TEXTINPUT: {
          char* ascii_value = event.text.text;

          while (*ascii_value) {
            // Microtan requires capitals for all commands, so swap upper/lower case for convenience
            uint8_t key = (uint8_t)*ascii_value;

            if (((key >= 'A') && (key <= 'Z')) || ((key >= 'a') && (key <= 'z'))) {
              key ^= 0x20;
            }

            keyboard_keypress(key);
            ascii_value++;
          }
        } break;

        case SDL_KEYDOWN: {
          SDL_KeyCode keycode = event.key.keysym.sym;

          if (keycode == SDLK_F1) {
            show_main_menu(renderer, &display_overwritten, file_dialog_directory, sizeof(file_dialog_directory));
          } else if (keycode == SDLK_F2) {
            keyboard_use_hex_keypad(true);
          } else if (keycode == SDLK_F3) {
            keyboard_use_hex_keypad(false);
          } else if (keycode == SDLK_F5) {
            system_reset();
          } else {
            if (keycode == SDLK_KP_ENTER) {
              keycode = 0x0a;
            } else if ((SDL_GetModState() & KMOD_CTRL) && (keycode >= 'a') && (keycode <= 'z')) {
              keycode -= 0x60;
            } else if ((SDL_GetModState() & KMOD_CTRL) && (keycode >= 'A') && (keycode <= 'Z')) {
              keycode -= 0x40;
            } else if (keycode == 0x08) {
              keycode = 0x7f;
            }

            if ((keycode < ' ') || (keycode == 0x7f)) {
              keyboard_keypress(keycode);
            }
          }
        } break;
      } // SDL event switch
    }   // SDL event loop

    // via_6522_print_regs();
    joystick();
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    long elapsed_time = (end_time.tv_sec - start_time.tv_sec) * 1000 + (end_time.tv_nsec - start_time.tv_nsec) / 1000000;

    if (elapsed_time < LOOP_EXECUTE_TIME_MS) {
      sleep_time.tv_sec = 0;
      sleep_time.tv_nsec = (LOOP_EXECUTE_TIME_MS - elapsed_time) * 1000000;
      nanosleep(&sleep_time, NULL);
    }
  } // main loop

  save_window_settings(window, file_dialog_directory);
  SDL_DestroyTexture(texture);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  system_close();

  return 0;
}
