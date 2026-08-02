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

#include "colour_vdu.h"
#include "cpu_6502.h"
#include "display.h"
#include "eprom.h"
#include "function_return_codes.h"
#include "invaders_sound.h"
#include "joystick.h"
#include "keyboard.h"
#include "menu_bar.h"
#include "popup.h"
#include "rtc.h"
#include "system.h"
#include "tandos.h"
#include "via_6522.h"

#define DISPLAY_SWITCH_REDRAW_FRAMES    10
#define MICROTAN_DEFAULT_CLOCK_FREQUENCY 750000
#define LOOP_EXECUTE_TIME_MS             20
#define MICROTAN_CLOCK_OPTION_COUNT      4

const char* SETTINGS_FILE = "microtan_settings.txt";

static const int MICROTAN_CLOCK_OPTIONS[MICROTAN_CLOCK_OPTION_COUNT] = {
  750000,
  1500000,
  3000000,
  6000000};

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static bool is_supported_clock_frequency(int clock_frequency);

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

void save_window_settings(SDL_Window* window, int cpu_clock_frequency, const char* file_dialog_directory) {
  int x, y, width, height;
  SDL_GetWindowSize(window, &width, &height);
  SDL_GetWindowPosition(window, &x, &y);
  FILE* file = fopen(SETTINGS_FILE, "w");

  if (file) {
    const char* save_directory = (file_dialog_directory && (*file_dialog_directory != '\0')) ? file_dialog_directory : ".";
    fprintf(file, "%d %d %d %d %d %d %d %lld\n%s\n",
            x, y, width, height, (int)display_get_hires_mode(),
            cpu_clock_frequency, colour_vdu_get_enabled() ? 1 : 0,
            (long long)rtc_get_offset_seconds(), save_directory);
    tandos_save_settings(file);
    fclose(file);
  }
}

void load_window_settings(int* x, int* y, int* width, int* height,
                          display_hires_mode_t* display_mode,
                          int* cpu_clock_frequency, bool* colour_vdu_enabled,
                          char* file_dialog_directory,
                          size_t file_dialog_directory_size) {
  FILE* file = fopen(SETTINGS_FILE, "r");
  char geometry_line[256];
  char path_line[PATH_MAX];
  *display_mode = DISPLAY_HIRES_MODE_NONE;
  *cpu_clock_frequency = MICROTAN_DEFAULT_CLOCK_FREQUENCY;
  *colour_vdu_enabled = false;
  choose_default_file_directory(file_dialog_directory, file_dialog_directory_size);
  rtc_set_offset_seconds(0);

  if (file) {
    int display_mode_raw = 0;
    int cpu_clock_frequency_raw = MICROTAN_DEFAULT_CLOCK_FREQUENCY;
    int colour_vdu_enabled_raw = 0;
    long long rtc_offset_seconds_raw = 0;
    int values_read = 0;

    if (fgets(geometry_line, sizeof(geometry_line), file) != NULL) {
      values_read = sscanf(geometry_line, "%d %d %d %d %d %d %d %lld",
                           x, y, width, height, &display_mode_raw,
                           &cpu_clock_frequency_raw, &colour_vdu_enabled_raw,
                           &rtc_offset_seconds_raw);
    }

    if (values_read < 4) {
      *x = SDL_WINDOWPOS_CENTERED;
      *y = SDL_WINDOWPOS_CENTERED;
    } else if ((values_read >= 5) &&
               (display_mode_raw >= (int)DISPLAY_HIRES_MODE_NONE) &&
               (display_mode_raw <= (int)DISPLAY_HIRES_MODE_COLOUR_VDU)) {
      *display_mode = (display_hires_mode_t)display_mode_raw;
    }

    if ((values_read >= 6) && is_supported_clock_frequency(cpu_clock_frequency_raw)) {
      *cpu_clock_frequency = cpu_clock_frequency_raw;
    }
    if (values_read >= 7) {
      *colour_vdu_enabled = colour_vdu_enabled_raw != 0;
    }
    if (values_read >= 8) {
      rtc_set_offset_seconds((int64_t)rtc_offset_seconds_raw);
    }

    // Optional persisted file-dialog directory line.
    if (fgets(path_line, sizeof(path_line), file) != NULL) {
      trim_trailing_newline(path_line);
      if ((path_line[0] != '\0') && directory_exists(path_line)) {
        snprintf(file_dialog_directory, file_dialog_directory_size, "%s", path_line);
      }
    }

    tandos_load_settings(file);
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

static void integer_scaled_display_size(int requested_height,
                                        int* width, int* height) {
  int native_width;
  int native_height;
  display_get_render_size(&native_width, &native_height);

  int requested_display_height = requested_height - MENU_BAR_HEIGHT;
  int scale = (requested_display_height + native_height / 2) / native_height;
  if (scale < 2) {
    scale = 2;
  }

  *width = native_width * scale;
  *height = native_height * scale + MENU_BAR_HEIGHT;
}

static void resize_window_to_integer_scale(SDL_Window* window,
                                           int requested_height) {
  int width;
  int height;
  int target_width;
  int target_height;
  SDL_GetWindowSize(window, &width, &height);
  integer_scaled_display_size(requested_height, &target_width, &target_height);

  if ((width != target_width) || (height != target_height)) {
    SDL_SetWindowSize(window, target_width, target_height);
  }
}

static bool is_supported_clock_frequency(int clock_frequency) {
  for (int i = 0; i < MICROTAN_CLOCK_OPTION_COUNT; i++) {
    if (MICROTAN_CLOCK_OPTIONS[i] == clock_frequency) {
      return true;
    }
  }

  return false;
}

static const char* path_base_name(const char* path) {
  const char* slash = path ? strrchr(path, '/') : NULL;
  const char* backslash = path ? strrchr(path, '\\') : NULL;
  const char* separator = slash;

  if (backslash && (!separator || (backslash > separator))) {
    separator = backslash;
  }
  return separator ? separator + 1 : (path ? path : "");
}

typedef enum {
  MENU_COMMAND_LOAD_PROGRAM = 1,
  MENU_COMMAND_SAVE_SNAPSHOT,
  MENU_COMMAND_EXPORT_HEX,
  MENU_COMMAND_QUIT,
  MENU_COMMAND_RESET = 10,
  MENU_COMMAND_CLOCK_750KHZ,
  MENU_COMMAND_CLOCK_1_5MHZ,
  MENU_COMMAND_CLOCK_3MHZ,
  MENU_COMMAND_CLOCK_6MHZ,
  MENU_COMMAND_TANDOS_TOGGLE = 20,
  MENU_COMMAND_DISK_UNIT_0,
  MENU_COMMAND_DISK_UNIT_1,
  MENU_COMMAND_DISK_UNIT_2,
  MENU_COMMAND_DISK_UNIT_3,
  MENU_COMMAND_DISK_UNIT_4,
  MENU_COMMAND_DISK_UNIT_5,
  MENU_COMMAND_DISK_UNIT_6,
  MENU_COMMAND_DISK_UNIT_7,
  MENU_COMMAND_DISK_CREATE,
  MENU_COMMAND_DISK_EJECT_ALL,
  MENU_COMMAND_DISPLAY_TEXT = 40,
  MENU_COMMAND_DISPLAY_TANGERINE,
  MENU_COMMAND_DISPLAY_GPU,
  MENU_COMMAND_DISPLAY_COLOUR_VDU,
  MENU_COMMAND_COLOUR_VDU_TOGGLE,
  MENU_COMMAND_INPUT_ASCII = 50,
  MENU_COMMAND_INPUT_HEX,
  MENU_COMMAND_HELP = 60
} application_menu_command_t;

typedef struct {
  menu_bar_menu_t menus[6];
  menu_bar_item_t file_items[5];
  menu_bar_item_t system_items[6];
  menu_bar_item_t disk_items[13];
  menu_bar_item_t display_items[6];
  menu_bar_item_t input_items[2];
  menu_bar_item_t help_items[1];
  char tandos_toggle_label[32];
  char disk_labels[TANDOS_UNIT_COUNT][160];
  char colour_vdu_toggle_label[40];
} application_menu_model_t;

static menu_bar_item_t menu_item(const char* label, const char* shortcut,
                                 int command, bool enabled, bool checked) {
  return (menu_bar_item_t){label, shortcut, command, enabled, checked};
}

static menu_bar_item_t menu_separator(void) {
  return menu_item(NULL, NULL, MENU_BAR_SEPARATOR_COMMAND, false, false);
}

static void build_application_menu(application_menu_model_t* model,
                                   int cpu_clock_frequency) {
  model->file_items[0] = menu_item("Load program...", NULL,
                                   MENU_COMMAND_LOAD_PROGRAM, true, false);
  model->file_items[1] = menu_item("Save snapshot...", NULL,
                                   MENU_COMMAND_SAVE_SNAPSHOT, true, false);
  model->file_items[2] = menu_item("Export Intel HEX...", NULL,
                                   MENU_COMMAND_EXPORT_HEX, true, false);
  model->file_items[3] = menu_separator();
  model->file_items[4] = menu_item("Quit", NULL, MENU_COMMAND_QUIT, true, false);

  model->system_items[0] = menu_item("Reset", "F5", MENU_COMMAND_RESET,
                                     true, false);
  model->system_items[1] = menu_separator();
  model->system_items[2] = menu_item("750 kHz (original)", NULL,
                                     MENU_COMMAND_CLOCK_750KHZ, true,
                                     cpu_clock_frequency == 750000);
  model->system_items[3] = menu_item("1.5 MHz", NULL,
                                     MENU_COMMAND_CLOCK_1_5MHZ, true,
                                     cpu_clock_frequency == 1500000);
  model->system_items[4] = menu_item("3 MHz", NULL,
                                     MENU_COMMAND_CLOCK_3MHZ, true,
                                     cpu_clock_frequency == 3000000);
  model->system_items[5] = menu_item("6 MHz", NULL,
                                     MENU_COMMAND_CLOCK_6MHZ, true,
                                     cpu_clock_frequency == 6000000);

  snprintf(model->tandos_toggle_label, sizeof(model->tandos_toggle_label),
           "%s TANDOS card", tandos_get_enabled() ? "Disable" : "Enable");
  model->disk_items[0] = menu_item(model->tandos_toggle_label, NULL,
                                   MENU_COMMAND_TANDOS_TOGGLE, true, false);
  model->disk_items[1] = menu_separator();
  for (int unit = 0; unit < TANDOS_UNIT_COUNT; unit++) {
    if (tandos_unit_mounted(unit)) {
      snprintf(model->disk_labels[unit], sizeof(model->disk_labels[unit]),
               "Drive %d: %s%s", unit,
               path_base_name(tandos_unit_file_name(unit)),
               tandos_unit_write_protected(unit) ? " [read-only]" : "");
    } else {
      snprintf(model->disk_labels[unit], sizeof(model->disk_labels[unit]),
               "Drive %d: <empty>", unit);
    }
    model->disk_items[unit + 2] = menu_item(
      model->disk_labels[unit], NULL, MENU_COMMAND_DISK_UNIT_0 + unit,
      true, false);
  }
  model->disk_items[10] = menu_separator();
  model->disk_items[11] = menu_item("Create blank disk image...", NULL,
                                    MENU_COMMAND_DISK_CREATE, true, false);
  model->disk_items[12] = menu_item("Eject all disks", NULL,
                                    MENU_COMMAND_DISK_EJECT_ALL, true, false);

  display_hires_mode_t display_mode = display_get_hires_mode();
  model->display_items[0] = menu_item("Text/chunky", NULL,
                                      MENU_COMMAND_DISPLAY_TEXT, true,
                                      display_mode == DISPLAY_HIRES_MODE_NONE);
  model->display_items[1] = menu_item("Tangerine hi-res (RGBI)", NULL,
                                      MENU_COMMAND_DISPLAY_TANGERINE, true,
                                      display_mode == DISPLAY_HIRES_MODE_TANGERINE);
  model->display_items[2] = menu_item("GPU", NULL,
                                      MENU_COMMAND_DISPLAY_GPU, true,
                                      display_mode == DISPLAY_HIRES_MODE_EXTENDED);
  model->display_items[3] = menu_item("Mousepacket Colour VDU", NULL,
                                      MENU_COMMAND_DISPLAY_COLOUR_VDU,
                                      colour_vdu_get_enabled(),
                                      display_mode == DISPLAY_HIRES_MODE_COLOUR_VDU);
  model->display_items[4] = menu_separator();
  snprintf(model->colour_vdu_toggle_label,
           sizeof(model->colour_vdu_toggle_label), "%s Colour VDU card",
           colour_vdu_get_enabled() ? "Disable" : "Enable");
  model->display_items[5] = menu_item(model->colour_vdu_toggle_label, NULL,
                                      MENU_COMMAND_COLOUR_VDU_TOGGLE,
                                      true, false);

  model->input_items[0] = menu_item("ASCII keyboard", "F3",
                                    MENU_COMMAND_INPUT_ASCII, true,
                                    !keyboard_using_hex_keypad());
  model->input_items[1] = menu_item("Hex keypad", "F2",
                                    MENU_COMMAND_INPUT_HEX, true,
                                    keyboard_using_hex_keypad());
  model->help_items[0] = menu_item("Keyboard shortcuts", NULL,
                                   MENU_COMMAND_HELP, true, false);

  model->menus[0] = (menu_bar_menu_t){"File", model->file_items, 5};
  model->menus[1] = (menu_bar_menu_t){"System", model->system_items, 6};
  model->menus[2] = (menu_bar_menu_t){"Disks", model->disk_items, 13};
  model->menus[3] = (menu_bar_menu_t){"Display", model->display_items, 6};
  model->menus[4] = (menu_bar_menu_t){"Input", model->input_items, 2};
  model->menus[5] = (menu_bar_menu_t){"Help", model->help_items, 1};
}

static void show_disk_unit_menu(SDL_Renderer* renderer, int unit,
                                char* file_dialog_directory,
                                size_t file_dialog_directory_size) {
  char title[160];
  if (tandos_unit_mounted(unit)) {
    snprintf(title, sizeof(title), "Drive %d: %s%s", unit,
             path_base_name(tandos_unit_file_name(unit)),
             tandos_unit_write_protected(unit) ? " [read-only]" : "");
  } else {
    snprintf(title, sizeof(title), "Drive %d: <empty>", unit);
  }

  const char* items[] = {
    "Mount disk image read/write",
    "Mount disk image read-only",
    "Eject disk",
    "Disk information"};
  int selection = popup_menu_select(renderer, title, items, 4, 0);
  if ((selection == 0) || (selection == 1)) {
    const char* extensions[] = {".img", ".tdsk", ".dsk", ".raw"};
    char file_name[PATH_MAX];
    if (popup_file_select(renderer, "Mount TANDOS Disk",
                          file_dialog_directory, extensions, 4, false, "",
                          file_name, sizeof(file_name))) {
      update_file_dialog_directory(file_name, file_dialog_directory,
                                   file_dialog_directory_size);
      int rv = tandos_mount(unit, file_name, selection == 1);
      if (rv == RV_OK) {
        tandos_set_enabled(true);
        popup_show(renderer, selection == 1
          ? "Disk mounted read-only; TANDOS card enabled."
          : "Disk mounted read/write; TANDOS card enabled.");
      } else {
        popup_show(renderer,
                   "Mount failed. Use a single-sided 35-80 track, "
                   "9 or 10 sector, 256-byte raw image.");
      }
    }
  } else if (selection == 2) {
    tandos_eject(unit);
  } else if (selection == 3) {
    char information[PATH_MAX + 160];
    if (tandos_unit_mounted(unit)) {
      snprintf(information, sizeof(information),
               "Drive %d:\n%s\n%d tracks, %d sectors/track\n%s",
               unit, tandos_unit_file_name(unit), tandos_unit_tracks(unit),
               tandos_unit_sectors_per_track(unit),
               tandos_unit_write_protected(unit) ? "Read-only" : "Read/write");
    } else {
      snprintf(information, sizeof(information), "Drive %d: empty", unit);
    }
    popup_show(renderer, information);
  }
}

static void create_blank_disk(SDL_Renderer* renderer,
                              char* file_dialog_directory,
                              size_t file_dialog_directory_size) {
  const char* extensions[] = {".img", ".tdsk"};
  char file_name[PATH_MAX];
  char geometry[64];
  if (!popup_file_select(renderer, "Create TANDOS Disk",
                         file_dialog_directory, extensions, 2, true,
                         "new_disk.img", file_name, sizeof(file_name))) {
    return;
  }
  update_file_dialog_directory(file_name, file_dialog_directory,
                               file_dialog_directory_size);
  if (!popup_prompt_input(renderer, "Create TANDOS Disk",
                          "Tracks,sectors per track (35-80, 9 or 10)",
                          "80,10", geometry, sizeof(geometry))) {
    return;
  }

  int tracks;
  int sectors;
  if ((sscanf(geometry, "%d,%d", &tracks, &sectors) != 2) ||
      (tandos_create_image(file_name, tracks, sectors) != RV_OK)) {
    popup_show(renderer, "Unable to create disk. Example geometry: 80,10");
  } else {
    popup_show(renderer,
               "Blank disk image created. Use TANDOS INIT before saving files.");
  }
}

static void execute_application_menu_command(
  SDL_Renderer* renderer, int command, bool* is_running,
  bool* display_overwritten, int* cpu_clock_frequency,
  char* file_dialog_directory, size_t file_dialog_directory_size) {
  if ((command >= MENU_COMMAND_DISK_UNIT_0) &&
      (command <= MENU_COMMAND_DISK_UNIT_7)) {
    show_disk_unit_menu(renderer, command - MENU_COMMAND_DISK_UNIT_0,
                        file_dialog_directory, file_dialog_directory_size);
    *display_overwritten = true;
    return;
  }

  if ((command >= MENU_COMMAND_DISPLAY_TEXT) &&
      (command <= MENU_COMMAND_DISPLAY_COLOUR_VDU)) {
    display_hires_mode_t mode = (display_hires_mode_t)(
      command - MENU_COMMAND_DISPLAY_TEXT);
    if ((mode != DISPLAY_HIRES_MODE_COLOUR_VDU) ||
        colour_vdu_get_enabled()) {
      bool is_tanbug_output =
        (mode == DISPLAY_HIRES_MODE_NONE) ||
        (mode == DISPLAY_HIRES_MODE_COLOUR_VDU);
      bool wants_colour_vdu = mode == DISPLAY_HIRES_MODE_COLOUR_VDU;
      if (is_tanbug_output && colour_vdu_get_enabled() &&
          (colour_vdu_output_selected() != wants_colour_vdu)) {
        keyboard_keypress(0x18);
      } else {
        display_set_hires_mode(mode);
      }
    }
    *display_overwritten = true;
    return;
  }

  switch (command) {
    case MENU_COMMAND_LOAD_PROGRAM: {
      const char* extensions[] = {".m65", ".hex", ".ihx", ".ihex"};
      char file_name[PATH_MAX];
      if (popup_file_select(renderer, "Load Program", file_dialog_directory,
                            extensions, 4, false, "", file_name,
                            sizeof(file_name))) {
        update_file_dialog_directory(file_name, file_dialog_directory,
                                     file_dialog_directory_size);
        system_reset();
        int rv = system_load_program_file(file_name);
        if ((rv == RV_OK) && (strstr(file_name, "berzerk") != NULL)) {
          keyboard_use_hex_keypad(true);
        }
        popup_show(renderer, rv == RV_OK
          ? "Program loaded."
          : "Load failed. See terminal output for details.");
      }
      break;
    }

    case MENU_COMMAND_SAVE_SNAPSHOT: {
      const char* extensions[] = {".m65"};
      char file_name[PATH_MAX];
      if (popup_file_select(renderer, "Save Snapshot", file_dialog_directory,
                            extensions, 1, true, "snapshot.m65", file_name,
                            sizeof(file_name))) {
        update_file_dialog_directory(file_name, file_dialog_directory,
                                     file_dialog_directory_size);
        int rv = system_save_m65_file(file_name);
        popup_show(renderer, rv == RV_OK
          ? "Snapshot saved."
          : "Save failed. See terminal output for details.");
      }
      break;
    }

    case MENU_COMMAND_EXPORT_HEX: {
      const char* extensions[] = {".hex", ".ihx", ".ihex"};
      char file_name[PATH_MAX];
      char range_input[128];
      if (!popup_file_select(renderer, "Save Intel HEX",
                             file_dialog_directory, extensions, 3, true,
                             "range.hex", file_name, sizeof(file_name))) {
        break;
      }
      update_file_dialog_directory(file_name, file_dialog_directory,
                                   file_dialog_directory_size);
      if (!popup_prompt_input(renderer, "Save Intel HEX",
                              "Address range start-end (hex)", "0200-03FF",
                              range_input, sizeof(range_input))) {
        break;
      }
      uint16_t start_address;
      uint16_t end_address;
      if (!parse_hex_range(range_input, &start_address, &end_address)) {
        popup_show(renderer, "Invalid range. Example: 0200-03FF");
        break;
      }
      int rv = system_save_intel_hex_range(file_name, start_address,
                                            end_address);
      popup_show(renderer, rv == RV_OK
        ? "Intel HEX saved."
        : "Save failed. See terminal output for details.");
      break;
    }

    case MENU_COMMAND_QUIT:
      *is_running = false;
      break;

    case MENU_COMMAND_RESET:
      system_reset();
      break;

    case MENU_COMMAND_CLOCK_750KHZ:
    case MENU_COMMAND_CLOCK_1_5MHZ:
    case MENU_COMMAND_CLOCK_3MHZ:
    case MENU_COMMAND_CLOCK_6MHZ:
      *cpu_clock_frequency = MICROTAN_CLOCK_OPTIONS[
        command - MENU_COMMAND_CLOCK_750KHZ];
      break;

    case MENU_COMMAND_TANDOS_TOGGLE:
      tandos_set_enabled(!tandos_get_enabled());
      break;

    case MENU_COMMAND_DISK_CREATE:
      create_blank_disk(renderer, file_dialog_directory,
                        file_dialog_directory_size);
      break;

    case MENU_COMMAND_DISK_EJECT_ALL:
      tandos_eject_all();
      break;

    case MENU_COMMAND_COLOUR_VDU_TOGGLE: {
      bool enabled = !colour_vdu_get_enabled();
      if (!enabled && colour_vdu_output_selected()) {
        keyboard_keypress(0x18);
      }
      colour_vdu_set_enabled(enabled);
      if (!enabled &&
          (display_get_hires_mode() == DISPLAY_HIRES_MODE_COLOUR_VDU)) {
        display_set_hires_mode(DISPLAY_HIRES_MODE_NONE);
      }
      break;
    }

    case MENU_COMMAND_INPUT_ASCII:
      keyboard_use_hex_keypad(false);
      break;

    case MENU_COMMAND_INPUT_HEX:
      keyboard_use_hex_keypad(true);
      break;

    case MENU_COMMAND_HELP:
      popup_show(renderer,
                 "Menu bar: mouse, arrows, Enter and Escape\n"
                 "F1: Open or close the File menu\n"
                 "F2: Select hex keypad input\n"
                 "F3: Select ASCII keyboard input\n"
                 "F5: Reset system\n"
                 "Ctrl+A to Ctrl+Z: send control characters\n"
                 "Backspace: send Microtan delete");
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

  int x = SDL_WINDOWPOS_CENTERED;
  int y = SDL_WINDOWPOS_CENTERED;
  int width = DISPLAY_WIDTH;
  int height = DISPLAY_HEIGHT;
  display_hires_mode_t saved_display_mode = DISPLAY_HIRES_MODE_NONE;
  int cpu_clock_frequency = MICROTAN_DEFAULT_CLOCK_FREQUENCY;
  bool saved_colour_vdu_enabled = false;
  char file_dialog_directory[PATH_MAX];
  load_window_settings(&x, &y, &width, &height, &saved_display_mode,
                       &cpu_clock_frequency, &saved_colour_vdu_enabled,
                       file_dialog_directory, sizeof(file_dialog_directory));
  colour_vdu_set_enabled(saved_colour_vdu_enabled);
  display_set_hires_mode(saved_display_mode);
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
  integer_scaled_display_size(height, &width, &height);
  SDL_Window* window = SDL_CreateWindow("Microtan 65", x, y, width, height, SDL_WINDOW_RESIZABLE);
  SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
  if (!renderer) {
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
  }
  if (!renderer) {
    fprintf(stderr, "Unable to create SDL renderer: %s\n", SDL_GetError());
    SDL_DestroyWindow(window);
    system_close();
    SDL_Quit();
    return 1;
  }
  menu_bar_state_t menu_bar;
  if (!menu_bar_initialise(&menu_bar)) {
    fprintf(stderr, "Unable to initialise menu bar.\n");
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    system_close();
    SDL_Quit();
    return 1;
  }
  application_menu_model_t menu_model;
  build_application_menu(&menu_model, cpu_clock_frequency);

  int render_width;
  int render_height;
  display_get_render_size(&render_width, &render_height);
  SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, render_width, render_height);
  SDL_Texture* scanlines = create_scanline_texture(renderer, render_width, render_height);
  uint32_t pixels[COLOUR_VDU_WIDTH * DISPLAY_HEIGHT];
  bool is_running = true;
  SDL_Event event;
  struct timespec start_time;
  struct timespec end_time;
  struct timespec sleep_time;
  bool display_overwritten = true;
  int forced_redraw_frames = 0;

  while (is_running) {
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    // Execute LOOP_EXECUTE_TIME_MS's worth of instructions
    cpu_6502_execute(cpu_clock_frequency * LOOP_EXECUTE_TIME_MS / 1000);

    if (colour_vdu_get_enabled() && colour_vdu_output_changed_event()) {
      display_set_hires_mode(colour_vdu_output_selected()
        ? DISPLAY_HIRES_MODE_COLOUR_VDU
        : DISPLAY_HIRES_MODE_NONE);
      display_overwritten = true;
      forced_redraw_frames = DISPLAY_SWITCH_REDRAW_FRAMES;
    }

    build_application_menu(&menu_model, cpu_clock_frequency);

    // If the display has been updated, re-render the window
    bool colour_vdu_updated = colour_vdu_updated_event();
    if ((display_updated_event()) ||
        ((display_get_hires_mode() == DISPLAY_HIRES_MODE_COLOUR_VDU) && colour_vdu_updated) ||
        (forced_redraw_frames > 0) ||
        (display_overwritten)) {
      display_overwritten = false;
      int required_width;
      int required_height;
      display_get_render_size(&required_width, &required_height);
      if ((required_width != render_width) || (required_height != render_height)) {
        SDL_DestroyTexture(texture);
        SDL_DestroyTexture(scanlines);
        render_width = required_width;
        render_height = required_height;
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                                    SDL_TEXTUREACCESS_STREAMING,
                                    render_width, render_height);
        scanlines = create_scanline_texture(renderer, render_width, render_height);

        SDL_GetWindowSize(window, &width, &height);
        resize_window_to_integer_scale(window, height);
        forced_redraw_frames = DISPLAY_SWITCH_REDRAW_FRAMES;
      }
      display_render(pixels);
      SDL_UpdateTexture(texture, NULL, pixels, render_width * sizeof(Uint32));
      SDL_RenderClear(renderer);
      SDL_GetWindowSize(window, &width, &height);
      SDL_Rect dest_rect = {0, MENU_BAR_HEIGHT, width,
                            height - MENU_BAR_HEIGHT};
      SDL_RenderCopy(renderer, texture, NULL, &dest_rect);
      SDL_RenderCopy(renderer, scanlines, NULL, &dest_rect);
      menu_bar_render(renderer, &menu_bar, menu_model.menus, 6);
      SDL_RenderPresent(renderer);
      if (forced_redraw_frames > 0) {
        forced_redraw_frames--;
      }
    }

    while (SDL_PollEvent(&event)) {
      int menu_command = MENU_BAR_SEPARATOR_COMMAND;
      if (menu_bar_handle_event(&menu_bar, renderer, &event,
                                menu_model.menus, 6, &menu_command)) {
        display_overwritten = true;
        if (menu_command != MENU_BAR_SEPARATOR_COMMAND) {
          execute_application_menu_command(
            renderer, menu_command, &is_running, &display_overwritten,
            &cpu_clock_frequency, file_dialog_directory,
            sizeof(file_dialog_directory));
          build_application_menu(&menu_model, cpu_clock_frequency);
        }
        continue;
      }

      switch (event.type) {
        case SDL_QUIT:
          is_running = false;
          break;

        case SDL_WINDOWEVENT:
          switch (event.window.event) {
            case SDL_WINDOWEVENT_SIZE_CHANGED:
            case SDL_WINDOWEVENT_RESIZED: {
              resize_window_to_integer_scale(window, event.window.data2);

              SDL_RenderClear(renderer);
              int width, height;
              SDL_GetWindowSize(window, &width, &height);
              SDL_Rect dest_rect = {0, MENU_BAR_HEIGHT, width,
                                    height - MENU_BAR_HEIGHT};
              SDL_RenderCopy(renderer, texture, NULL, &dest_rect);
              SDL_RenderCopy(renderer, scanlines, NULL, &dest_rect);
              menu_bar_render(renderer, &menu_bar, menu_model.menus, 6);
              SDL_RenderPresent(renderer);
              display_overwritten = true;
              forced_redraw_frames = DISPLAY_SWITCH_REDRAW_FRAMES;
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

          if (keycode == SDLK_F2) {
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

  save_window_settings(window, cpu_clock_frequency, file_dialog_directory);
  menu_bar_close(&menu_bar);
  SDL_DestroyTexture(texture);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  system_close();
  SDL_Quit();

  return 0;
}
