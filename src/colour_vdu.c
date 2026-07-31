#include <stdio.h>
#include <string.h>
#include <time.h>

#include "colour_vdu.h"
#include "external_filenames.h"
#include "function_return_codes.h"
#include "system.h"

#define COLOUR_VDU_BASE          0xA000
#define COLOUR_VDU_RAM_SIZE      0x0800
#define COLOUR_VDU_CRTC_BASE     0xA7F0
#define COLOUR_VDU_LINE_WIDTH    0xA640
#define COLOUR_VDU_COLUMNS       64
#define STANDARD_DISPLAY_COLUMNS 32
#define STANDARD_CURSOR_ADDRESS  0x03E0
#define COLOUR_VDU_ROWS          25
#define COLOUR_VDU_CELL_WIDTH    6
#define COLOUR_VDU_CELL_HEIGHT   10
#define COLOUR_VDU_VISIBLE_CELLS (COLOUR_VDU_COLUMNS * COLOUR_VDU_ROWS)
#define CRTC_REGISTER_COUNT      18
#define SAA5050_ALPHA_GLYPHS     96
#define SAA5050_MOSAIC_GLYPHS    64
#define SAA5050_GLYPH_COUNT      (SAA5050_ALPHA_GLYPHS + (2 * SAA5050_MOSAIC_GLYPHS))
#define SAA5050_CHARACTER_SIZE   10
#define SAA5050_DATA_SIZE        (SAA5050_GLYPH_COUNT * SAA5050_CHARACTER_SIZE)

static uint8_t colour_vdu_ram[COLOUR_VDU_RAM_SIZE];
static uint8_t crtc_registers[CRTC_REGISTER_COUNT];
static uint8_t crtc_selected_register;
static uint8_t character_data[SAA5050_DATA_SIZE];
static bool colour_vdu_enabled;
static bool colour_vdu_updated = true;
static bool output_changed;
static bool output_selected;
static bool tanbug_initialisation_pending;
static bool tanbug_force_standard_output;
static unsigned int last_flash_phase;
static unsigned int last_cursor_phase;

static const uint32_t teletext_palette[8] = {
  0x000000FF,
  0xFF0000FF,
  0x00FF00FF,
  0xFFFF00FF,
  0x0000FFFF,
  0xFF00FFFF,
  0x00FFFFFF,
  0xFFFFFFFF};

typedef struct teletext_state_t {
  uint8_t foreground;
  uint8_t background;
  uint8_t held_mosaic;
  bool graphics;
  bool flash;
  bool conceal;
  bool separated;
  bool hold;
  bool double_height;
  bool have_held_mosaic;
} teletext_state_t;

static unsigned int colour_vdu_time_ms(void) {
  struct timespec now;
  timespec_get(&now, TIME_UTC);
  return (unsigned int)((uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U);
}

static uint8_t colour_vdu_crtc_mask(uint8_t reg, uint8_t value) {
  static const uint8_t masks[CRTC_REGISTER_COUNT] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0x1F, 0x7F, 0x7F, 0x03,
    0x1F, 0x7F, 0x1F, 0x3F, 0xFF, 0x3F, 0xFF, 0x3F, 0xFF};
  return value & masks[reg];
}

static uint8_t colour_vdu_read(uint16_t address) {
  if (!colour_vdu_enabled) {
    if (tanbug_force_standard_output &&
        (address == COLOUR_VDU_LINE_WIDTH)) {
      *system_get_memory_pointer(address) = STANDARD_DISPLAY_COLUMNS;
      tanbug_force_standard_output = false;
    }
    return *system_get_memory_pointer(address);
  }

  if (address < COLOUR_VDU_CRTC_BASE) {
    return colour_vdu_ram[address - COLOUR_VDU_BASE];
  }

  if ((address & 1U) == 0) {
    return crtc_selected_register;
  }

  if (crtc_selected_register < CRTC_REGISTER_COUNT) {
    return crtc_registers[crtc_selected_register];
  }
  return 0;
}

static void colour_vdu_write(uint16_t address, uint8_t value) {
  if (!colour_vdu_enabled) {
    if (tanbug_initialisation_pending &&
        (address == COLOUR_VDU_LINE_WIDTH) &&
        (value == COLOUR_VDU_COLUMNS)) {
      // TANBUG V3B defaults to its 64-column output. Select the standard
      // 32-column display when the Colour VDU card is absent.
      tanbug_force_standard_output = true;
      *system_get_memory_pointer(0x000A) =
        (uint8_t)(STANDARD_CURSOR_ADDRESS & 0xFF);
      *system_get_memory_pointer(0x000B) =
        (uint8_t)(STANDARD_CURSOR_ADDRESS >> 8);
      tanbug_initialisation_pending = false;
    }
    return;
  }

  if (address < COLOUR_VDU_CRTC_BASE) {
    if (address == COLOUR_VDU_LINE_WIDTH) {
      tanbug_initialisation_pending = false;
    }
    if ((address == COLOUR_VDU_LINE_WIDTH) &&
        ((value == STANDARD_DISPLAY_COLUMNS) ||
         (value == COLOUR_VDU_COLUMNS))) {
      // TANBUG uses its current line width as the selected-output state.
      output_selected = value == COLOUR_VDU_COLUMNS;
      output_changed = true;
    }

    colour_vdu_ram[address - COLOUR_VDU_BASE] = value;
    colour_vdu_updated = true;
    return;
  }

  if ((address & 1U) == 0) {
    crtc_selected_register = value & 0x1F;
  } else if (crtc_selected_register < CRTC_REGISTER_COUNT) {
    crtc_registers[crtc_selected_register] =
      colour_vdu_crtc_mask(crtc_selected_register, value);
    colour_vdu_updated = true;
  }
}

static void colour_vdu_set_pixel(uint32_t* pixels, int x, int y, uint32_t colour) {
  if ((x >= 0) && (x < COLOUR_VDU_WIDTH) &&
      (y >= 0) && (y < COLOUR_VDU_HEIGHT)) {
    pixels[y * COLOUR_VDU_WIDTH + x] = colour;
  }
}

static void colour_vdu_fill_cell(uint32_t* pixels, int cell_x, int cell_y,
                                 uint32_t colour) {
  int left = cell_x * COLOUR_VDU_CELL_WIDTH;
  int top = cell_y * COLOUR_VDU_CELL_HEIGHT;

  for (int y = 0; y < COLOUR_VDU_CELL_HEIGHT; y++) {
    for (int x = 0; x < COLOUR_VDU_CELL_WIDTH; x++) {
      colour_vdu_set_pixel(pixels, left + x, top + y, colour);
    }
  }
}

typedef enum colour_vdu_height_part_t {
  COLOUR_VDU_HEIGHT_NORMAL,
  COLOUR_VDU_HEIGHT_TOP,
  COLOUR_VDU_HEIGHT_BOTTOM
} colour_vdu_height_part_t;

static int colour_vdu_glyph_row(int output_row,
                                colour_vdu_height_part_t height_part) {
  if (height_part == COLOUR_VDU_HEIGHT_TOP) {
    return output_row / 2;
  }
  if (height_part == COLOUR_VDU_HEIGHT_BOTTOM) {
    return (COLOUR_VDU_CELL_HEIGHT / 2) + (output_row / 2);
  }
  return output_row;
}

static void colour_vdu_draw_alpha(uint32_t* pixels, int cell_x, int cell_y,
                                  uint8_t character, uint32_t foreground,
                                  uint32_t background,
                                  colour_vdu_height_part_t height_part) {
  int left = cell_x * COLOUR_VDU_CELL_WIDTH;
  int top = cell_y * COLOUR_VDU_CELL_HEIGHT;
  int glyph_index = (character & 0x7F) - 0x20;
  const uint8_t* glyph =
    character_data + glyph_index * SAA5050_CHARACTER_SIZE;

  for (int y = 0; y < COLOUR_VDU_CELL_HEIGHT; y++) {
    uint8_t row = glyph[colour_vdu_glyph_row(y, height_part)];

    for (int x = 0; x < COLOUR_VDU_CELL_WIDTH; x++) {
      uint32_t colour = (row & (0x20U >> x)) ? foreground : background;
      colour_vdu_set_pixel(pixels, left + x, top + y, colour);
    }
  }
}

static void colour_vdu_draw_mosaic(uint32_t* pixels, int cell_x, int cell_y,
                                   uint8_t mosaic, bool separated,
                                   uint32_t foreground, uint32_t background,
                                   colour_vdu_height_part_t height_part) {
  int left = cell_x * COLOUR_VDU_CELL_WIDTH;
  int top = cell_y * COLOUR_VDU_CELL_HEIGHT;
  int glyph_index = SAA5050_ALPHA_GLYPHS + mosaic;
  if (separated) {
    glyph_index += SAA5050_MOSAIC_GLYPHS;
  }
  const uint8_t* glyph =
    character_data + glyph_index * SAA5050_CHARACTER_SIZE;

  for (int y = 0; y < COLOUR_VDU_CELL_HEIGHT; y++) {
    uint8_t row = glyph[colour_vdu_glyph_row(y, height_part)];
    for (int x = 0; x < COLOUR_VDU_CELL_WIDTH; x++) {
      uint32_t colour = (row & (0x20U >> x)) ? foreground : background;
      colour_vdu_set_pixel(pixels, left + x, top + y, colour);
    }
  }
}
static bool colour_vdu_is_mosaic(uint8_t character) {
  uint8_t code = character & 0x7F;
  return ((code >= 0x20) && (code <= 0x3F)) ||
         ((code >= 0x60) && (code <= 0x7F));
}

static uint8_t colour_vdu_mosaic_bits(uint8_t character) {
  uint8_t code = character & 0x7F;
  return (uint8_t)((code & 0x1F) | ((code & 0x40) >> 1));
}

static void colour_vdu_apply_control(teletext_state_t* state, uint8_t code) {
  if (code <= 0x07) {
    state->foreground = code;
    state->graphics = false;
    state->conceal = false;
  } else if ((code >= 0x10) && (code <= 0x17)) {
    state->foreground = code - 0x10;
    state->graphics = true;
    state->conceal = false;
  } else {
    switch (code) {
      case 0x08:
        state->flash = true;
        break;
      case 0x09:
        state->flash = false;
        break;
      case 0x0C:
        state->double_height = false;
        break;
      case 0x0D:
        state->double_height = true;
        break;
      case 0x18:
        state->conceal = true;
        break;
      case 0x19:
        state->separated = false;
        break;
      case 0x1A:
        state->separated = true;
        break;
      case 0x1C:
        state->background = 0;
        break;
      case 0x1D:
        state->background = state->foreground;
        break;
      case 0x1E:
        state->hold = true;
        break;
      case 0x1F:
        state->hold = false;
        state->have_held_mosaic = false;
        break;
      default:
        break;
    }
  }
}

static bool colour_vdu_cursor_visible(unsigned int now_ms) {
  uint8_t cursor_start = crtc_registers[10];
  uint8_t cursor_mode = (cursor_start >> 5) & 0x03;

  if (cursor_mode == 1) {
    return false;
  }
  if (cursor_mode == 0) {
    return true;
  }
  return ((now_ms / ((cursor_mode == 2) ? 500U : 250U)) & 1U) == 0;
}

void colour_vdu_render(uint32_t* pixels) {
  unsigned int now_ms = colour_vdu_time_ms();
  bool flash_visible = ((now_ms / 500U) & 1U) == 0;
  bool cursor_visible = colour_vdu_cursor_visible(now_ms);
  uint16_t start_address =
    (uint16_t)(((crtc_registers[12] & 0x3F) << 8) | crtc_registers[13]);
  uint16_t cursor_address =
    (uint16_t)(((crtc_registers[14] & 0x3F) << 8) | crtc_registers[15]);
  int displayed_rows = crtc_registers[6];
  if (displayed_rows > COLOUR_VDU_ROWS) {
    displayed_rows = COLOUR_VDU_ROWS;
  }
  bool lower_half_valid[COLOUR_VDU_COLUMNS] = {false};
  bool next_lower_half_valid[COLOUR_VDU_COLUMNS];

  for (int i = 0; i < COLOUR_VDU_WIDTH * COLOUR_VDU_HEIGHT; i++) {
    pixels[i] = teletext_palette[0];
  }

  for (int row = 0; row < displayed_rows; row++) {
    memset(next_lower_half_valid, 0, sizeof(next_lower_half_valid));
    teletext_state_t state = {
      .foreground = 7,
      .background = 0,
      .held_mosaic = 0,
      .graphics = false,
      .flash = false,
      .conceal = false,
      .separated = false,
      .hold = false,
      .double_height = false,
      .have_held_mosaic = false};

    for (int column = 0; column < COLOUR_VDU_COLUMNS; column++) {
      uint16_t display_address =
        (uint16_t)((start_address + row * COLOUR_VDU_COLUMNS + column) & 0x07FF);
      uint8_t raw_character = colour_vdu_ram[display_address];
      uint8_t character = raw_character & 0x7F;
      bool inverse = (raw_character & 0x80) != 0;
      bool render_cell = !lower_half_valid[column];
      bool render_lower = render_cell && state.double_height &&
                          (row + 1 < displayed_rows);
      colour_vdu_height_part_t height_part = state.double_height
        ? COLOUR_VDU_HEIGHT_TOP
        : COLOUR_VDU_HEIGHT_NORMAL;
      uint32_t foreground = teletext_palette[state.foreground];
      uint32_t background = teletext_palette[state.background];

      if (inverse) {
        uint32_t swap = foreground;
        foreground = background;
        background = swap;
      }

      if (character < 0x20) {
        if (render_cell) {
          if (state.graphics && (state.hold || (character == 0x1E)) &&
              state.have_held_mosaic) {
            colour_vdu_draw_mosaic(pixels, column, row, state.held_mosaic,
                                   state.separated, foreground, background,
                                   height_part);
            if (render_lower) {
              colour_vdu_draw_mosaic(pixels, column, row + 1,
                                     state.held_mosaic, state.separated,
                                     foreground, background,
                                     COLOUR_VDU_HEIGHT_BOTTOM);
            }
          } else {
            colour_vdu_fill_cell(pixels, column, row, background);
            if (render_lower) {
              colour_vdu_fill_cell(pixels, column, row + 1, background);
            }
          }
        }
        colour_vdu_apply_control(&state, character);
      } else if (state.conceal || (state.flash && !flash_visible)) {
        if (render_cell) {
          colour_vdu_fill_cell(pixels, column, row, background);
          if (render_lower) {
            colour_vdu_fill_cell(pixels, column, row + 1, background);
          }
        }
      } else if (state.graphics && colour_vdu_is_mosaic(character)) {
        uint8_t mosaic = colour_vdu_mosaic_bits(character);
        if (render_cell) {
          colour_vdu_draw_mosaic(pixels, column, row, mosaic, state.separated,
                                 foreground, background, height_part);
          if (render_lower) {
            colour_vdu_draw_mosaic(pixels, column, row + 1, mosaic,
                                   state.separated, foreground, background,
                                   COLOUR_VDU_HEIGHT_BOTTOM);
          }
        }
        state.held_mosaic = mosaic;
        state.have_held_mosaic = true;
      } else if (render_cell) {
        colour_vdu_draw_alpha(pixels, column, row, character,
                              foreground, background, height_part);
        if (render_lower) {
          colour_vdu_draw_alpha(pixels, column, row + 1, character,
                                foreground, background,
                                COLOUR_VDU_HEIGHT_BOTTOM);
        }
      }

      if (render_lower) {
        next_lower_half_valid[column] = true;
      }

      if (cursor_visible && (display_address == (cursor_address & 0x07FF))) {
        int cursor_start = crtc_registers[10] & 0x1F;
        int cursor_end = crtc_registers[11] & 0x1F;
        if (cursor_start >= COLOUR_VDU_CELL_HEIGHT) {
          cursor_start = COLOUR_VDU_CELL_HEIGHT - 1;
        }
        if (cursor_end >= COLOUR_VDU_CELL_HEIGHT) {
          cursor_end = COLOUR_VDU_CELL_HEIGHT - 1;
        }
        if (cursor_end < cursor_start) {
          cursor_end = cursor_start;
        }

        int left = column * COLOUR_VDU_CELL_WIDTH;
        int top = row * COLOUR_VDU_CELL_HEIGHT;
        for (int y = cursor_start; y <= cursor_end; y++) {
          for (int x = 0; x < COLOUR_VDU_CELL_WIDTH; x++) {
            uint32_t* pixel =
              &pixels[(top + y) * COLOUR_VDU_WIDTH + left + x];
            *pixel ^= 0xFFFFFF00U;
          }
        }
      }
    }

    memcpy(lower_half_valid, next_lower_half_valid,
           sizeof(lower_half_valid));
  }

  colour_vdu_updated = false;
}
bool colour_vdu_updated_event(void) {
  unsigned int now_ms = colour_vdu_time_ms();
  unsigned int flash_phase = now_ms / 500U;
  unsigned int cursor_phase = now_ms / 250U;
  bool updated = colour_vdu_updated ||
                 (flash_phase != last_flash_phase) ||
                 (cursor_phase != last_cursor_phase);

  last_flash_phase = flash_phase;
  last_cursor_phase = cursor_phase;
  colour_vdu_updated = false;
  return updated;
}

void colour_vdu_set_enabled(bool enabled) {
  colour_vdu_enabled = enabled;
  if (!enabled) {
    output_changed = false;
    output_selected = false;
  }
  colour_vdu_updated = true;
}

bool colour_vdu_get_enabled(void) {
  return colour_vdu_enabled;
}

bool colour_vdu_output_changed_event(void) {
  bool changed = output_changed;
  output_changed = false;
  return changed;
}

bool colour_vdu_output_selected(void) {
  return output_selected;
}

void colour_vdu_reset(uint8_t bank, uint16_t address) {
  (void)bank;
  (void)address;

  memset(crtc_registers, 0, sizeof(crtc_registers));
  crtc_registers[1] = COLOUR_VDU_COLUMNS;
  crtc_registers[6] = COLOUR_VDU_ROWS;
  crtc_registers[9] = COLOUR_VDU_CELL_HEIGHT - 1;
  crtc_registers[10] = 0x20 | (COLOUR_VDU_CELL_HEIGHT - 2);
  crtc_registers[11] = COLOUR_VDU_CELL_HEIGHT - 1;
  crtc_selected_register = 0;
  output_changed = false;
  output_selected = false;
  tanbug_initialisation_pending = true;
  colour_vdu_updated = true;
}

int colour_vdu_initialise(uint8_t bank, uint16_t address, uint16_t param,
                          char* identifier) {
  (void)bank;
  (void)param;
  (void)identifier;

  FILE* character_file = fopen(SAA5050_CHR_FILENAME, "rb");
  if (!character_file) {
    printf("Error opening [%s]\r\n", SAA5050_CHR_FILENAME);
    return RV_FILE_OPEN_ERROR;
  }

  fseek(character_file, 0, SEEK_END);
  long file_size = ftell(character_file);
  fseek(character_file, 0, SEEK_SET);
  if (file_size != SAA5050_DATA_SIZE) {
    fclose(character_file);
    printf("Invalid file [%s]\r\n", SAA5050_CHR_FILENAME);
    return RV_INVALID_FILE;
  }

  size_t bytes_read =
    fread(character_data, 1, sizeof(character_data), character_file);
  fclose(character_file);
  if (bytes_read != sizeof(character_data)) {
    printf("Error reading [%s]\r\n", SAA5050_CHR_FILENAME);
    return RV_FILE_READ_ERROR;
  }

  memset(colour_vdu_ram, 0x20, sizeof(colour_vdu_ram));
  colour_vdu_enabled = false;
  output_changed = false;
  output_selected = false;

  return system_register_memory_mapped_device(
    address, address + COLOUR_VDU_RAM_SIZE - 1,
    colour_vdu_read, colour_vdu_write, false);
}
