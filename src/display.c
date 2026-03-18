#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "display.h"
#include "external_filenames.h"
#include "function_return_codes.h"
#include "system.h"

// GPU register address offsets
#define NUM_GPU_REGISTERS         0x80 // 128 registers
#define GPU_SPRITE_COLLISION_LIST 0x20 // 64 values, to 0x5f
#define GPU_PLANE_WRITE_MASK      0x7b
#define GPU_PLANE_DISPLAY_MASK    0x7c
#define GPU_RANDOM_REGISTER       0x7d
#define GPU_STATUS_REGISTER       0x7e
#define GPU_OPERATION_REGISTER    0x7f
#define GPU_RESULT0_REGISTER      0x1d
#define GPU_RESULT1_REGISTER      0x1e
#define GPU_ERROR_REGISTER        0x1f
#define GPU_COLLISION_ID_REGISTER 0x20
#define GPU_COLLISION_COUNT       0x21
#define GPU_COLLISION_FIRST       0x22

#define GPU_STATUS_OK             0x00
#define GPU_STATUS_ADDR_RANGE     0x01
#define GPU_STATUS_ALLOCATION     0x02
#define GPU_STATUS_INVALID_ID     0x03
#define GPU_STATUS_INACTIVE       0x04
#define GPU_STATUS_BAD_OPCODE     0x05

#define NUM_GPU_PLANES       4
#define MAX_STAMPS           256
#define MAX_SPRITES          64
#define SPRITE_FLAGS_ENABLED (1 << 0)
#define SPRITE_FLAGS_VISIBLE (1 << 1)

typedef struct sprite_t {
    bool active;
    int16_t x;
    int16_t y;
    uint8_t width;
    uint8_t height;
    uint8_t group;
    uint8_t collision_group;
    uint8_t flags;
    uint8_t* image_ptr;
} sprite_t, *sprite_ptr_t;

// Microtan graphics
static uint8_t charset_rom[4096];
static uint8_t chunky_rom[4096];
static uint8_t white_array[8192];
static uint8_t* text_display_ram = NULL;
static uint8_t chunky_graphics_bits[512];
static bool chunky_bit = false;

// 4x Hi-Res graphics boards
static const char* hires_identifier[4] = {"red", "green", "blue", "intensity"};
static uint8_t display_hires_memory[4][8192];
static uint8_t display_hires_bank[4];
static uint16_t display_hires_start_address[4];
static uint8_t display_hires_selected_bank = 0;

// Extended graphics "GPU board"
static uint16_t gpu_registers_address = 0x0000;
static uint8_t border_left = 0;
static uint8_t border_top = 0;
static uint8_t border_right = 0;
static uint8_t border_bottom = 0;
static uint8_t gpu_reg[NUM_GPU_REGISTERS];
static uint8_t gpu_pixels[DISPLAY_WIDTH][DISPLAY_HEIGHT];
static uint8_t* gpu_stamp_table[MAX_STAMPS];
static sprite_t gpu_sprite_table[MAX_SPRITES];
static uint8_t palette_red[256];
static uint8_t palette_green[256];
static uint8_t palette_blue[256];

static display_hires_mode_t hires_mode = DISPLAY_HIRES_MODE_NONE;
static bool display_updated = true;
static bool sprite_is_enabled(const sprite_t* sprite);
static bool sprite_is_visible(const sprite_t* sprite);

static uint8_t main_display_read_callback(uint16_t address) {
  (void)address;
  return 0;
}

void main_display_write_callback(uint16_t address, uint8_t value) {
  (void)value;
  chunky_graphics_bits[address - 0x0200] = chunky_bit;
  display_updated = true;
}

static uint8_t chunky_enable_callback(uint16_t address) {
  (void)address;
  chunky_bit = true;
  return 0;
}

void chunky_disable_callback(uint16_t address, uint8_t value) {
  (void)address;
  (void)value;
  chunky_bit = false;
}

static uint8_t hires_display_read_callback(uint16_t address) {
  uint8_t current_bank = display_hires_selected_bank;
  uint8_t* main_ram = system_get_memory_pointer(0x0000);
  for (int index = 0; index < 4; index++) {
    if (display_hires_bank[index] == current_bank) {
      return display_hires_memory[index][address - display_hires_start_address[index]];
    }
  }
  return main_ram[address];
}

void hires_display_write_callback(uint16_t address, uint8_t value) {
  uint8_t current_bank = display_hires_selected_bank;
  for (int index = 0; index < 4; index++) {
    if (display_hires_bank[index] == current_bank) {
      display_hires_memory[index][address - display_hires_start_address[index]] = value;
      break;
    }
  }
  display_updated = true;
}

static void hires_bank_select_write_callback(uint16_t address, uint8_t value) {
  (void)address;
  display_hires_selected_bank = value;
}

void display_render(uint32_t* pixels) {
  // Render the text display into the white_array pixel map
  uint8_t* character_value = text_display_ram;
  uint8_t* chunky_bit = chunky_graphics_bits;

  if (NULL == character_value) {
    memset(white_array, 0xAA, sizeof(white_array));
  } else {
    for (int i = 0; i < 512; i++) {
      // Get the address in the white_array of the top line of this character
      uint8_t* b = white_array + (i & 0x1f) + (i >> 5) * (32 * 16);
      // Get a pointer to the character set rom for this character
      uint8_t* character_data = ((*chunky_bit) ? chunky_rom : charset_rom) + (*character_value) * 16;

      // Copy the character data into the white_array
      for (int row = 0; row < 16; row++) {
        *b = *character_data++;
        b += 32;
      }

      character_value++;
      chunky_bit++;
    }
  }

  // Render the text and hi-res graphics cards into the pixels array, which is then
  // used to draw the window
  switch (hires_mode) {
    case DISPLAY_HIRES_MODE_NONE: {
      for (int i = 0; i < 8192; i++) {
        for (int bit = 0; bit < 8; bit++) {
          int x = (i * 8 + bit) % DISPLAY_WIDTH;
          int y = (i * 8 + bit) / DISPLAY_WIDTH;
          uint8_t w_bit = (white_array[i] >> (7 - bit)) & 1;
          uint32_t w = w_bit * 0xffffffff;
          pixels[y * DISPLAY_WIDTH + x] = w | 0xff;
        }
      }
    } break;

    case DISPLAY_HIRES_MODE_TANGERINE: {
      for (int i = 0; i < 8192; i++) {
        for (int bit = 0; bit < 8; bit++) {
          int x = (i * 8 + bit) % DISPLAY_WIDTH;
          int y = (i * 8 + bit) / DISPLAY_WIDTH;
          uint8_t w_bit = (white_array[i] >> (7 - bit)) & 1;
          uint8_t r_bit = (display_hires_memory[0][i] >> (7 - bit)) & 1;
          uint8_t g_bit = (display_hires_memory[1][i] >> (7 - bit)) & 1;
          uint8_t b_bit = (display_hires_memory[2][i] >> (7 - bit)) & 1;
          uint8_t intensity = ((display_hires_memory[3][i] >> (7 - bit)) & 1) ? 0xff : 0x80;
          uint32_t w = w_bit * 0xffffffff;
          uint32_t r = r_bit * intensity;
          uint32_t g = g_bit * intensity;
          uint32_t b = b_bit * intensity;
          pixels[y * DISPLAY_WIDTH + x] = w | (r << 24) | (g << 16) | (b << 8) | 0xff;
        }
      }
    } break;

    case DISPLAY_HIRES_MODE_EXTENDED: {
      bool show_gpu = (gpu_reg[GPU_PLANE_DISPLAY_MASK] & ((1 << NUM_GPU_PLANES) - 1)) != 0;
      for (int y = 0; y < DISPLAY_HEIGHT; y++) {
        for (int x = 0; x < DISPLAY_WIDTH; x++) {
          int i = (y * 32) + (x >> 3);
          uint32_t w = ((white_array[i] >> (7 - (x & 0x07))) & 1) * 0xffffffff;
          uint32_t red = 0;
          uint32_t green = 0;
          uint32_t blue = 0;
          if (show_gpu &&
              (x >= border_left) && (x <= 255 - border_right) &&
              (y >= border_top) && (y <= 255 - border_bottom)) {
            red = palette_red[gpu_pixels[x][y]];
            green = palette_green[gpu_pixels[x][y]];
            blue = palette_blue[gpu_pixels[x][y]];
          }
          pixels[y * DISPLAY_WIDTH + x] = w | (red << 24) | (green << 16) | (blue << 8) | 0xff;
        }
      }

      sprite_ptr_t sprite = gpu_sprite_table;
      for (int i = 0; i < MAX_SPRITES; i++) {
        if ((sprite->active) && sprite_is_enabled(sprite) && sprite_is_visible(sprite) && (NULL != sprite->image_ptr) &&
            (sprite->width > 0) && (sprite->height > 0)) {
          uint32_t red = 0;
          uint32_t green = 0;
          uint32_t blue = 0;
          uint8_t* sprite_pixel = sprite->image_ptr;
          for (int sy = 0; sy < sprite->height; sy++) {
            int y = sprite->y + sy;
            if ((y < 0) || (y >= DISPLAY_HEIGHT)) {
              sprite_pixel += sprite->width;
              continue;
            }
            for (int sx = 0; sx < sprite->width; sx++) {
              int x = sprite->x + sx;
              if ((*sprite_pixel != 0xff) && (x >= 0) && (x < DISPLAY_WIDTH)) {
                red = palette_red[*sprite_pixel];
                green = palette_green[*sprite_pixel];
                blue = palette_blue[*sprite_pixel];
                pixels[y * DISPLAY_WIDTH + x] = (red << 24) | (green << 16) | (blue << 8) | 0xff;
              }
              sprite_pixel++;
            }
          }
        }
        sprite++;
      }
    } break;
  }
}

bool display_updated_event() {
  bool rv = display_updated;
  display_updated = false;
  display_updated = false;
  return rv;
}

void display_set_hires_mode(display_hires_mode_t new_mode) {
  if ((new_mode < DISPLAY_HIRES_MODE_NONE) || (new_mode > DISPLAY_HIRES_MODE_EXTENDED)) {
    return;
  }

  hires_mode = new_mode;
  display_updated = true;
}

display_hires_mode_t display_get_hires_mode() {
  return hires_mode;
}

uint8_t* display_get_hires_memory_pointer(int board_index) {
  if ((board_index >= 0) && (board_index <= 3)) {
    return &display_hires_memory[board_index][0];
  } else {
    return NULL;
  }
}

void display_load_chunky_memory(uint8_t* src) {
  memcpy(chunky_graphics_bits, src, sizeof(chunky_graphics_bits));
}

void display_save_chunky_memory(uint8_t* dest) {
  for (int i = 0; i < 64; i++) {
    uint8_t packed = 0;

    for (int bit = 0; bit < 8; bit++) {
      if (chunky_graphics_bits[i * 8 + bit]) {
        packed |= (uint8_t)(1U << bit);
      }
    }

    dest[i] = packed;
  }
}

static bool sprite_is_enabled(const sprite_t* sprite) {
  return (sprite->flags & SPRITE_FLAGS_ENABLED) != 0;
}

static bool sprite_is_visible(const sprite_t* sprite) {
  return (sprite->flags & SPRITE_FLAGS_VISIBLE) != 0;
}

void display_gpu_set_colour(uint8_t x, uint8_t y, uint8_t colour) {
  uint8_t write_mask = gpu_reg[GPU_PLANE_WRITE_MASK] & 0x0f;

  if (write_mask == 0) {
    return;
  }

  if (write_mask == 0x0f) {
    gpu_pixels[x][y] = colour;
  } else {
    // Treat plane bits as nibble masks over low/high nibble pairs.
    uint8_t full_mask = (uint8_t)(write_mask | (write_mask << 4));
    gpu_pixels[x][y] = (gpu_pixels[x][y] & (uint8_t)~full_mask) | (colour & full_mask);
  }

  display_updated = true;
}

uint8_t display_gpu_get_colour(uint8_t x, uint8_t y) {
  return gpu_pixels[x][y];
}

void display_gpu_draw_line(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, uint8_t colour) {
  int dx = x2 - x1;
  int dy = y2 - y1;

  int stepx = 1;
  int stepy = 1;

  if (dy < 0) {
    dy = -dy;
    stepy = -1;
  }
  if (dx < 0) {
    dx = -dx;
    stepx = -1;
  }
  dy <<= 1;
  dx <<= 1;

  display_gpu_set_colour(x1, y1, colour);

  if (dx > dy) {
    int fraction = dy - (dx >> 1);
    while (x1 != x2) {
      if (fraction >= 0) {
        y1 += stepy;
        fraction -= dx;
      }
      x1 += stepx;
      fraction += dy;
      display_gpu_set_colour(x1, y1, colour);
    }
  } else {
    int fraction = dx - (dy >> 1);
    while (y1 != y2) {
      if (fraction >= 0) {
        x1 += stepx;
        fraction -= dy;
      }
      y1 += stepy;
      fraction += dx;
      display_gpu_set_colour(x1, y1, colour);
    }
  }
}

void display_gpu_draw_ellipse(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, uint8_t colour) {
  int dx = abs(x2 - x1);
  int dy = abs(y2 - y1);
  int rx = dx / 2;
  int ry = dy / 2;
  int xc = (x1 + x2) / 2;
  int yc = (y1 + y2) / 2;

  int x = 0, y = ry;
  int p = (ry * ry) - (rx * rx * ry) + ((rx * rx) / 4);
  while ((2 * x * ry * ry) < (2 * y * rx * rx)) {
    display_gpu_set_colour(xc + x, yc - y, colour);
    display_gpu_set_colour(xc - x, yc + y, colour);
    display_gpu_set_colour(xc + x, yc + y, colour);
    display_gpu_set_colour(xc - x, yc - y, colour);

    if (p < 0) {
      x = x + 1;
      p = p + (2 * ry * ry * x) + (ry * ry);
    } else {
      x = x + 1;
      y = y - 1;
      p = p + (2 * ry * ry * x + ry * ry) - (2 * rx * rx * y);
    }
  }

  p = ((x + 0.5) * (x + 0.5) * ry * ry) + ((y - 1) * (y - 1) * rx * rx) - (rx * rx * ry * ry);
  while (y >= 0) {
    display_gpu_set_colour(xc + x, yc - y, colour);
    display_gpu_set_colour(xc - x, yc + y, colour);
    display_gpu_set_colour(xc + x, yc + y, colour);
    display_gpu_set_colour(xc - x, yc - y, colour);

    if (p > 0) {
      y = y - 1;
      p = p - (2 * rx * rx * y) + (rx * rx);
    } else {
      y = y - 1;
      x = x + 1;
      p = p + (2 * ry * ry * x) - (2 * rx * rx * y) - (rx * rx);
    }
  }
}

void display_draw_horizontal_line(uint8_t x1, uint8_t x2, uint8_t y, uint8_t colour) {
  for (int x = x1; x <= x2; x++) {
    display_gpu_set_colour(x, y, colour);
  }
}

void display_gpu_fill_ellipse(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, uint8_t colour) {
  int dx = abs(x2 - x1);
  int dy = abs(y2 - y1);
  int rx = dx / 2;
  int ry = dy / 2;
  int xc = (x1 + x2) / 2;
  int yc = (y1 + y2) / 2;

  int x = 0, y = ry;
  int p = (ry * ry) - (rx * rx * ry) + ((rx * rx) / 4);
  while ((2 * x * ry * ry) < (2 * y * rx * rx)) {
    display_draw_horizontal_line(xc - x, xc + x, yc - y, colour);
    display_draw_horizontal_line(xc - x, xc + x, yc + y, colour);

    if (p < 0) {
      x = x + 1;
      p = p + (2 * ry * ry * x) + (ry * ry);
    } else {
      x = x + 1;
      y = y - 1;
      p = p + (2 * ry * ry * x + ry * ry) - (2 * rx * rx * y);
    }
  }

  p = ((x + 0.5) * (x + 0.5) * ry * ry) + ((y - 1) * (y - 1) * rx * rx) - (rx * rx * ry * ry);
  while (y >= 0) {
    display_draw_horizontal_line(xc - x, xc + x, yc - y, colour);
    display_draw_horizontal_line(xc - x, xc + x, yc + y, colour);

    if (p > 0) {
      y = y - 1;
      p = p - (2 * rx * rx * y) + (rx * rx);
    } else {
      y = y - 1;
      x = x + 1;
      p = p + (2 * ry * ry * x) - (2 * rx * rx * y) - (rx * rx);
    }
  }
}

void display_gpu_draw_triangle(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, uint8_t x3, uint8_t y3, uint8_t colour) {
  display_gpu_draw_line(x1, y1, x2, y2, colour);
  display_gpu_draw_line(x2, y2, x3, y3, colour);
  display_gpu_draw_line(x3, y3, x1, y1, colour);
}

void display_gpu_fill_triangle(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, uint8_t x3, uint8_t y3, uint8_t colour) {
  // Find bounding box
  uint8_t minX = x1 < x2 ? (x1 < x3 ? x1 : x3) : (x2 < x3 ? x2 : x3);
  uint8_t minY = y1 < y2 ? (y1 < y3 ? y1 : y3) : (y2 < y3 ? y2 : y3);
  uint8_t maxX = x1 > x2 ? (x1 > x3 ? x1 : x3) : (x2 > x3 ? x2 : x3);
  uint8_t maxY = y1 > y2 ? (y1 > y3 ? y1 : y3) : (y2 > y3 ? y2 : y3);

  // Iterate through bounding box
  for (uint8_t y = minY; y <= maxY; y++) {
    for (uint8_t x = minX; x <= maxX; x++) {
      // Barycentric coordinates
      int w1 = (x - x2) * (y3 - y2) - (x3 - x2) * (y - y2);
      int w2 = (x - x3) * (y1 - y3) - (x1 - x3) * (y - y3);
      int w3 = (x - x1) * (y2 - y1) - (x2 - x1) * (y - y1);

      // If p is on or inside all edges, render pixel
      if (w1 >= 0 && w2 >= 0 && w3 >= 0) {
        display_gpu_set_colour(x, y, colour);
      }
    }
  }
}

void display_gpu_draw_rectangle(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, uint8_t colour) {
  if (y1 > y2) {
    uint8_t t = y1;
    y1 = y2;
    y2 = t;
  }
  if (x1 > x2) {
    uint8_t t = x1;
    x1 = x2;
    x2 = t;
  }
  display_draw_horizontal_line(x1, x2, y1, colour);
  display_draw_horizontal_line(x1, x2, y2, colour);
  for (int y = y1; y <= y2; y++) {
    display_gpu_set_colour(x1, y, colour);
    display_gpu_set_colour(x2, y, colour);
  }
}

void display_gpu_fill_rectangle(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2, uint8_t colour) {
  if (y1 > y2) {
    uint8_t t = y1;
    y1 = y2;
    y2 = t;
  }
  if (x1 > x2) {
    uint8_t t = x1;
    x1 = x2;
    x2 = t;
  }
  for (int y = y1; y <= y2; y++) {
    display_draw_horizontal_line(x1, x2, y, colour);
  }
}

void display_gpu_scroll(int h, int v, uint8_t colour) {
  if (h > 0) {
    for (int y = 0; y < DISPLAY_HEIGHT; ++y) {
      for (int x = DISPLAY_WIDTH - 1; x >= h; --x) {
        gpu_pixels[x][y] = gpu_pixels[x - h][y];
      }
      for (int x = 0; x < h; ++x) {
        gpu_pixels[x][y] = colour;
      }
    }
  } else if (h < 0) {
    for (int y = 0; y < DISPLAY_HEIGHT; ++y) {
      for (int x = 0; x < DISPLAY_WIDTH + h; ++x) {
        gpu_pixels[x][y] = gpu_pixels[x - h][y];
      }
      for (int x = DISPLAY_WIDTH + h; x < DISPLAY_WIDTH; ++x) {
        gpu_pixels[x][y] = colour;
      }
    }
  }

  if (v > 0) {
    for (int x = 0; x < DISPLAY_WIDTH; ++x) {
      for (int y = DISPLAY_HEIGHT - 1; y >= v; --y) {
        gpu_pixels[x][y] = gpu_pixels[x][y - v];
      }
      for (int y = 0; y < v; ++y) {
        gpu_pixels[x][y] = colour;
      }
    }
  } else if (v < 0) {
    for (int x = 0; x < DISPLAY_WIDTH; ++x) {
      for (int y = 0; y < DISPLAY_HEIGHT + v; ++y) {
        gpu_pixels[x][y] = gpu_pixels[x][y - v];
      }
      for (int y = DISPLAY_HEIGHT + v; y < DISPLAY_HEIGHT; ++y) {
        gpu_pixels[x][y] = colour;
      }
    }
  }
}

void display_gpu_stamp_create(uint8_t id, uint8_t width, uint8_t height, uint16_t image_address) {
  if (((int)image_address + ((int)width * (int)height)) > 0xffff) {
    gpu_reg[GPU_ERROR_REGISTER] = GPU_STATUS_ADDR_RANGE;
    return;
  }
  if (NULL != gpu_stamp_table[id]) {
    free(gpu_stamp_table[id]);
  }
  gpu_stamp_table[id] = malloc((int)width * (int)height + 2);
  if (NULL == gpu_stamp_table[id]) {
    gpu_reg[GPU_ERROR_REGISTER] = GPU_STATUS_ALLOCATION;
    return;
  }
  gpu_stamp_table[id][0] = width;
  gpu_stamp_table[id][1] = height;
  uint8_t* src = system_get_memory_pointer(image_address);
  memcpy(gpu_stamp_table[id] + 2, src, (int)width * (int)height);
  gpu_reg[GPU_ERROR_REGISTER] = GPU_STATUS_OK;
}

void display_gpu_stamp_place(uint8_t id, int16_t px, int16_t py) {
  if (NULL == gpu_stamp_table[id]) {
    gpu_reg[GPU_ERROR_REGISTER] = GPU_STATUS_ALLOCATION;
    return;
  }

  uint8_t width = gpu_stamp_table[id][0];
  uint8_t height = gpu_stamp_table[id][1];
  uint8_t* image = gpu_stamp_table[id] + 2;
  for (int y = py; y < py + height; y++) {
    for (int x = px; x < px + width; x++) {
      if ((y >= 0) && (y <= 0xff) && (x >= 0) && (x <= 0xff)) {
        display_gpu_set_colour(x, y, *image);
      }
      image++;
    }
  }
  gpu_reg[GPU_ERROR_REGISTER] = GPU_STATUS_OK;
}

void display_gpu_sprite_create(uint8_t id, int16_t x, int16_t y, uint8_t group, uint8_t collision_group, uint8_t flags, uint8_t width, uint8_t height, uint16_t image_address) {
  if (id >= MAX_SPRITES) {
    gpu_reg[GPU_ERROR_REGISTER] = GPU_STATUS_INVALID_ID;
    return;
  }

  if (((int)image_address + ((int)width * (int)height)) > 0xffff) {
    gpu_reg[GPU_ERROR_REGISTER] = GPU_STATUS_ADDR_RANGE;
    return;
  }

  if (NULL != gpu_sprite_table[id].image_ptr) {
    free(gpu_sprite_table[id].image_ptr);
  }
  gpu_sprite_table[id].image_ptr = malloc((int)width * (int)height + 2);
  if (NULL == gpu_sprite_table[id].image_ptr) {
    gpu_reg[GPU_ERROR_REGISTER] = GPU_STATUS_ALLOCATION;
    return;
  }

  gpu_sprite_table[id].x = x;
  gpu_sprite_table[id].y = y;
  gpu_sprite_table[id].width = width;
  gpu_sprite_table[id].height = height;
  gpu_sprite_table[id].group = group;
  gpu_sprite_table[id].collision_group = collision_group;
  if (flags == 0) {
    flags = SPRITE_FLAGS_ENABLED | SPRITE_FLAGS_VISIBLE;
  }
  gpu_sprite_table[id].flags = flags;

  uint8_t* src = system_get_memory_pointer(image_address);
  memcpy(gpu_sprite_table[id].image_ptr, src, (int)width * (int)height);

  gpu_sprite_table[id].active = true;

  gpu_reg[GPU_ERROR_REGISTER] = GPU_STATUS_OK;
}

void display_gpu_sprite_move(uint8_t id, uint16_t x, uint16_t y) {
  if (id >= MAX_SPRITES) {
    gpu_reg[GPU_ERROR_REGISTER] = GPU_STATUS_INVALID_ID;
    return;
  }
  if (!gpu_sprite_table[id].active || !sprite_is_enabled(&gpu_sprite_table[id])) {
    gpu_reg[GPU_ERROR_REGISTER] = GPU_STATUS_INACTIVE;
    return;
  }
  gpu_sprite_table[id].x = x;
  gpu_sprite_table[id].y = y;
  gpu_reg[GPU_ERROR_REGISTER] = GPU_STATUS_OK;
}

void display_gpu_sprite_set_flags(uint8_t id, uint8_t flags) {
  if (id >= MAX_SPRITES) {
    gpu_reg[GPU_ERROR_REGISTER] = GPU_STATUS_INVALID_ID;
    return;
  }
  if (!gpu_sprite_table[id].active) {
    gpu_reg[GPU_ERROR_REGISTER] = GPU_STATUS_INACTIVE;
    return;
  }
  gpu_sprite_table[id].flags = flags;
  gpu_reg[GPU_ERROR_REGISTER] = GPU_STATUS_OK;
}

void display_gpu_sprite_detect_collisions(uint8_t id) {
  uint8_t number_of_collisions = 0;
  gpu_reg[GPU_COLLISION_ID_REGISTER] = 0xff;
  gpu_reg[GPU_COLLISION_COUNT] = 0x00;
  for (int i = GPU_COLLISION_FIRST; i < GPU_COLLISION_FIRST + MAX_SPRITES; i++)
    gpu_reg[i] = 0xff;

  if (id >= MAX_SPRITES) {
    gpu_reg[GPU_ERROR_REGISTER] = GPU_STATUS_INVALID_ID;
    return;
  }
  if (!gpu_sprite_table[id].active || !sprite_is_enabled(&gpu_sprite_table[id])) {
    gpu_reg[GPU_ERROR_REGISTER] = GPU_STATUS_INACTIVE;
    return;
  }

  gpu_reg[GPU_COLLISION_ID_REGISTER] = id;
  for (int j = 0; j < MAX_SPRITES; j++) {
    if ((!gpu_sprite_table[j].active) || !sprite_is_enabled(&gpu_sprite_table[j]) || (id == j)) {
      continue;
    }

    if ((gpu_sprite_table[id].group & gpu_sprite_table[j].collision_group) ||
        (gpu_sprite_table[j].group & gpu_sprite_table[id].collision_group)) {
      if ((gpu_sprite_table[id].x < gpu_sprite_table[j].x + gpu_sprite_table[j].width) &&
          (gpu_sprite_table[id].x + gpu_sprite_table[id].width > gpu_sprite_table[j].x) &&
          (gpu_sprite_table[id].y < gpu_sprite_table[j].y + gpu_sprite_table[j].height) &&
          (gpu_sprite_table[id].y + gpu_sprite_table[id].height > gpu_sprite_table[j].y)) {
        int x_overlap_start = (gpu_sprite_table[id].x > gpu_sprite_table[j].x) ? gpu_sprite_table[id].x : gpu_sprite_table[j].x;
        int y_overlap_start = (gpu_sprite_table[id].y > gpu_sprite_table[j].y) ? gpu_sprite_table[id].y : gpu_sprite_table[j].y;
        int x_overlap_end = (gpu_sprite_table[id].x + gpu_sprite_table[id].width < gpu_sprite_table[j].x + gpu_sprite_table[j].width) ? gpu_sprite_table[id].x + gpu_sprite_table[id].width : gpu_sprite_table[j].x + gpu_sprite_table[j].width;
        int y_overlap_end = (gpu_sprite_table[id].y + gpu_sprite_table[id].height < gpu_sprite_table[j].y + gpu_sprite_table[j].height) ? gpu_sprite_table[id].y + gpu_sprite_table[id].height : gpu_sprite_table[j].y + gpu_sprite_table[j].height;

        for (int y = y_overlap_start; y < y_overlap_end; y++) {
          for (int x = x_overlap_start; x < x_overlap_end; x++) {
            uint8_t pixel1 = gpu_sprite_table[id].image_ptr[(y - gpu_sprite_table[id].y) * gpu_sprite_table[id].width + (x - gpu_sprite_table[id].x)];
            uint8_t pixel2 = gpu_sprite_table[j].image_ptr[(y - gpu_sprite_table[j].y) * gpu_sprite_table[j].width + (x - gpu_sprite_table[j].x)];

            if ((pixel1 != 0xff) && (pixel2 != 0xff)) {
              gpu_reg[GPU_COLLISION_FIRST + number_of_collisions++] = j;
              goto next_sprite;
            }
          }
        }
      }
    }
  next_sprite:
    continue;
  }
  gpu_reg[GPU_ERROR_REGISTER] = GPU_STATUS_OK;
  gpu_reg[GPU_COLLISION_COUNT] = number_of_collisions;
  return;
}

static uint8_t display_gpu_read_callback(uint16_t address) {
  uint8_t reg = address - gpu_registers_address;
  gpu_reg[GPU_RANDOM_REGISTER] = rand() & 0xff;
  if (reg < NUM_GPU_REGISTERS) {
    return gpu_reg[reg];
  } else {
    return 0xff;
  }
}

void display_gpu_write_callback(uint16_t address, uint8_t value) {
  uint8_t reg = address - gpu_registers_address;
  if (reg >= NUM_GPU_REGISTERS) {
    return;
  }

  gpu_reg[reg] = value;
  if (reg == GPU_OPERATION_REGISTER) {
    gpu_reg[GPU_STATUS_REGISTER] = GPU_STATUS_OK;
    switch (value) {
      case 0x00:
        display_gpu_set_colour(gpu_reg[1], gpu_reg[2], gpu_reg[0]);
        display_updated = true;
        break;

      case 0x01:
        gpu_reg[GPU_RESULT0_REGISTER] = display_gpu_get_colour(gpu_reg[1], gpu_reg[2]);
        break;

      case 0x10:
        display_gpu_draw_line(gpu_reg[1], gpu_reg[2], gpu_reg[3], gpu_reg[4], gpu_reg[0]);
        display_updated = true;
        break;

      case 0x11:
        display_gpu_draw_line(gpu_reg[1], gpu_reg[2], gpu_reg[3], gpu_reg[4], gpu_reg[0]);
        gpu_reg[1] = gpu_reg[3];
        gpu_reg[2] = gpu_reg[4];
        display_updated = true;
        break;

      case 0x20:
        display_gpu_draw_triangle(gpu_reg[1], gpu_reg[2], gpu_reg[3], gpu_reg[4], gpu_reg[5], gpu_reg[6], gpu_reg[0]);
        display_updated = true;
        break;

      case 0x21:
        display_gpu_fill_triangle(gpu_reg[1], gpu_reg[2], gpu_reg[3], gpu_reg[4], gpu_reg[5], gpu_reg[6], gpu_reg[0]);
        display_updated = true;
        break;

      case 0x30:
        display_gpu_draw_rectangle(gpu_reg[1], gpu_reg[2], gpu_reg[3], gpu_reg[4], gpu_reg[0]);
        display_updated = true;
        break;

      case 0x31:
        display_gpu_fill_rectangle(gpu_reg[1], gpu_reg[2], gpu_reg[3], gpu_reg[4], gpu_reg[0]);
        display_updated = true;
        break;

      case 0x40:
        display_gpu_draw_ellipse(gpu_reg[1], gpu_reg[2], gpu_reg[3], gpu_reg[4], gpu_reg[0]);
        display_updated = true;
        break;

      case 0x41:
        display_gpu_fill_ellipse(gpu_reg[1], gpu_reg[2], gpu_reg[3], gpu_reg[4], gpu_reg[0]);
        display_updated = true;
        break;

      case 0x80:
        display_gpu_stamp_create(gpu_reg[1], gpu_reg[2], gpu_reg[3], ((uint16_t)gpu_reg[4]) | ((uint16_t)gpu_reg[5] << 8));
        break;

      case 0x81:
        display_gpu_stamp_place(gpu_reg[1], (int16_t)((gpu_reg[3] << 8) | gpu_reg[2]), (int16_t)((gpu_reg[5] << 8) | gpu_reg[4]));
        display_updated = true;
        break;

      case 0x90:
        display_gpu_sprite_create(gpu_reg[0x01],                                   // id number
                                  (int16_t)((gpu_reg[0x03] << 8) | gpu_reg[0x02]), // x
                                  (int16_t)((gpu_reg[0x05] << 8) | gpu_reg[0x04]), // y
                                  gpu_reg[0x06],
                                  gpu_reg[0x07], // group, collision group
                                  gpu_reg[0x08], // flags
                                  gpu_reg[0x09],
                                  gpu_reg[0x0a],                                    // width, height
                                  (int16_t)((gpu_reg[0x0c] << 8) | gpu_reg[0x0b])); // image address
        break;

      case 0x91:
        display_gpu_sprite_move(gpu_reg[1], (int16_t)((gpu_reg[3] << 8) | gpu_reg[2]), (int16_t)((gpu_reg[5] << 8) | gpu_reg[4]));
        display_updated = true;
        break;

      case 0x92:
        display_gpu_sprite_set_flags(gpu_reg[1], gpu_reg[2]);
        display_updated = true;
        break;

      case 0x93:
        display_gpu_sprite_detect_collisions(gpu_reg[1]);
        break;

      case 0xe0:
        display_gpu_scroll((int)(int8_t)gpu_reg[1], (int)(int8_t)gpu_reg[2], gpu_reg[0]);
        display_updated = true;
        break;

      case 0xF0:
        border_left = gpu_reg[1];
        border_top = gpu_reg[2];
        border_right = gpu_reg[3];
        border_bottom = gpu_reg[4];
        display_updated = true;
        break;

      default:
        gpu_reg[GPU_STATUS_REGISTER] = GPU_STATUS_BAD_OPCODE;
        break;
    }
  }
}

void display_close() {
  for (int i = 0; i < MAX_STAMPS; i++) {
    if (NULL != gpu_stamp_table[i]) {
      free(gpu_stamp_table[i]);
      gpu_stamp_table[i] = NULL;
    }
  }
}

int display_initialise(uint8_t bank, uint16_t address, uint16_t param, char* identifier) {
  /*
  ** Main microtan display
  ** - load the text character set ROM - duplicates 0x00-0x7f into 0x80-0xff
  ** - create the chunky graphics characters
  ** - randomise the display RAM to make it look like the original system on boot
  */
  if (strcmp(identifier, "main display") == 0) {
    // Register the display with the system memory controller
    system_register_memory_mapped_device(address, address + 0x1ff, main_display_read_callback, main_display_write_callback, true);
    system_register_memory_mapped_device(param, param, chunky_enable_callback, NULL, false);
    system_register_memory_mapped_device(param + 3, param + 3, NULL, chunky_disable_callback, false);
    system_register_memory_mapped_device(0x8000, 0x9fff, hires_display_read_callback, hires_display_write_callback, false);
    system_register_memory_mapped_device(0xffff, 0xffff, NULL, hires_bank_select_write_callback, false);
    text_display_ram = system_get_memory_pointer(address);

    // Load in the character set ROM
    FILE* charset_rom_file = fopen(CHARSET_ROM_FILENAME, "rb");

    if (!charset_rom_file) {
      printf("Error opening [%s]\r\n", CHARSET_ROM_FILENAME);
      return RV_FILE_OPEN_ERROR;
    }

    // Check the file size - if not 2048 then it's not the right file
    fseek(charset_rom_file, 0, SEEK_END);
    size_t charset_rom_file_size = ftell(charset_rom_file);

    if (charset_rom_file_size != 2048) {
      fclose(charset_rom_file);
      printf("Invalid file [%s]\r\n", CHARSET_ROM_FILENAME);
      return RV_INVALID_FILE;
    }

    fseek(charset_rom_file, 0, SEEK_SET);
    // Read the file into the charaacter set ROM array
    size_t bytes_read = fread(charset_rom, 1, charset_rom_file_size, charset_rom_file);
    fclose(charset_rom_file);

    if (bytes_read != charset_rom_file_size) {
      printf("Error reading [%s] (read %lu, expect %lu)\r\n", CHARSET_ROM_FILENAME, bytes_read, charset_rom_file_size);
      return RV_FILE_READ_ERROR;
    }

    // Duplicate it, so that 0x80-0xFF display the same characters as 0x00-0x7F
    memcpy(charset_rom + 2048, charset_rom, 2048);
    // Create the chunky character ROM
    const uint8_t row_pixels[4] = {0x00, 0xf0, 0x0f, 0xff};

    for (int c = 0; c < 256; c++) {
      for (int row = 0; row < 16; row++) {
        chunky_rom[c * 16 + row] = row_pixels[(c >> ((row >> 1) & 0x0e)) & 0x03];
      }
    }

    // Randomise the display RAM
    for (int i = 0; i < 512; i++) {
      text_display_ram[i] = rand() & 0xff;
      chunky_graphics_bits[i] = rand() & 0x01;
    }
  }

  /* Initialise the four Tangerine hi-res boards.
  ** Just clears the RAM so that the user can see the normal display
  ** This is for convenience - the real system didn't do this.
  */
  else if (strncmp(identifier, "hires", 5) == 0) {
    int index;
    for (index = 0; (index < 4) && (strstr(identifier, hires_identifier[index]) == 0); index++)
      ;
    if (index < 4) {
      display_hires_bank[index] = bank;
      display_hires_start_address[index] = address;
      memset(&display_hires_memory[index][0], 0, 8192);
      if (display_hires_selected_bank == 0) {
        display_hires_selected_bank = bank;
      }
    }
  }

  /*
  ** Initialise the GPU board.  This isn't based on a real board, but
  ** is just something nice to play with.
  **
  ** API contract is documented in docs/GPU_REGISTER_SPEC.md.
  ** Programs write parameters to registers, then write opcode to $7F.
  ** The command executes immediately.
  */
  else if (strcmp(identifier, "gpu") == 0) {
    system_register_memory_mapped_device(address, address + NUM_GPU_REGISTERS - 1, display_gpu_read_callback, display_gpu_write_callback, true);
    gpu_registers_address = address;

    memset(gpu_reg, 0, sizeof(gpu_reg));
    memset(gpu_pixels, 0, sizeof(gpu_pixels));
    memset(gpu_stamp_table, 0, sizeof(gpu_stamp_table));
    memset(gpu_sprite_table, 0, sizeof(gpu_sprite_table));
    gpu_reg[GPU_PLANE_WRITE_MASK] = 0x0f;
    gpu_reg[GPU_PLANE_DISPLAY_MASK] = 0x01;
    gpu_reg[GPU_STATUS_REGISTER] = GPU_STATUS_OK;
    gpu_reg[GPU_ERROR_REGISTER] = GPU_STATUS_OK;

    // Create a standard 256 colour palette
    for (int i = 0; i < 16; i++) {
      palette_red[i] = i * 0x11;
      palette_green[i] = i * 0x11;
      palette_blue[i] = i * 0x11;
    }

    // Standard colors
    uint8_t stdColors[16] =
      {
        0x00,
        0x00,
        0x00,
        0x00,
        0x80,
        0x80,
        0x80,
        0x80,
        0xC0,
        0xC0,
        0xC0,
        0xC0,
        0xFF,
        0xFF,
        0xFF,
        0xFF};
    for (int i = 0; i < 16; i++) {
      palette_red[i + 16] = stdColors[i];
      palette_green[i + 16] = stdColors[(i + 2) & 0x0f];
      palette_blue[i + 16] = stdColors[(i + 4) & 0x0f];
    }

    // Levels for the red, green, and blue components in 6x8x5 RGB cube
    uint8_t colour_levels[6] = {0x00, 0x33, 0x66, 0x99, 0xCC, 0xFF};

    // Starting index for the RGB cube in the palette
    int start_index = 32;

    // Generate the 6x8x5 RGB cube
    for (int r = 0; r < 6; r++) {
      for (int g = 0; g < 6; g++) {
        for (int b = 0; b < 6; b++) {
          palette_red[start_index + r * 36 + g * 6 + b] = colour_levels[r];
          palette_green[start_index + r * 36 + g * 6 + b] = colour_levels[g];
          palette_blue[start_index + r * 36 + g * 6 + b] = colour_levels[b];
        }
      }
    }
  }

  return RV_OK;
}
