#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <dirent.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

#include "external_filenames.h"

#define POPUP_MAX_LINES 64
#define POPUP_MAX_ITEMS 32
#define POPUP_BUTTON_GAP 10
#define POPUP_BUTTON_MIN_WIDTH 96

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static void popup_get_renderer_size(SDL_Renderer* renderer, int* width, int* height) {
  int w = 800;
  int h = 600;

  if (renderer) {
    int out_w = 0;
    int out_h = 0;

    if ((SDL_GetRendererOutputSize(renderer, &out_w, &out_h) == 0) && (out_w > 0) && (out_h > 0)) {
      w = out_w;
      h = out_h;
    }
  }

  *width = w;
  *height = h;
}

static SDL_Rect popup_center_rect(SDL_Renderer* renderer, int desired_w, int desired_h, int margin) {
  int window_w;
  int window_h;
  popup_get_renderer_size(renderer, &window_w, &window_h);

  int max_w = window_w - (margin * 2);
  int max_h = window_h - (margin * 2);
  if (max_w < 120) {
    max_w = window_w;
  }
  if (max_h < 120) {
    max_h = window_h;
  }

  SDL_Rect rect;
  rect.w = desired_w;
  rect.h = desired_h;

  if (rect.w > max_w) {
    rect.w = max_w;
  }
  if (rect.h > max_h) {
    rect.h = max_h;
  }
  if (rect.w < 120) {
    rect.w = 120;
  }
  if (rect.h < 120) {
    rect.h = 120;
  }

  rect.x = (window_w - rect.w) / 2;
  rect.y = (window_h - rect.h) / 2;
  return rect;
}

static int popup_font_size_for_renderer(SDL_Renderer* renderer) {
  int width;
  int height;
  popup_get_renderer_size(renderer, &width, &height);

  int min_dim = (width < height) ? width : height;
  if (min_dim < 420) {
    return 14;
  }
  if (min_dim < 560) {
    return 16;
  }
  if (min_dim < 720) {
    return 18;
  }
  if (min_dim < 900) {
    return 20;
  }
  return 24;
}

static bool popup_find_font_in_directory(const char* directory, char* font_path, size_t font_path_size) {
  DIR* d = opendir(directory);

  if (!d) {
    return false;
  }

  struct dirent* dir;

  while ((dir = readdir(d)) != NULL) {
    char* dot = strrchr(dir->d_name, '.');

    if (dot && (strcasecmp(dot, ".ttf") == 0)) {
      size_t name_length = strlen(dir->d_name);

      if ((directory[0] == '.') && (directory[1] == '\0')) {
        if (name_length + 1 > font_path_size) {
          continue;
        }

        memcpy(font_path, dir->d_name, name_length + 1);
      } else {
        size_t directory_length = strlen(directory);
        size_t required_length = directory_length + 1 + name_length + 1;
        if (required_length > font_path_size) {
          continue;
        }

        memcpy(font_path, directory, directory_length);
        font_path[directory_length] = '/';
        memcpy(font_path + directory_length + 1, dir->d_name, name_length + 1);
      }
      break;
    }
  }

  closedir(d);
  return font_path[0] != '\0';
}

static bool popup_find_font(char* font_path, size_t font_path_size) {
  const char* font_directories[] = {
    ASSETS_FONTS_DIRECTORY,
    ASSETS_DIRECTORY,
    "."};

  font_path[0] = '\0';

  for (size_t i = 0; i < (sizeof(font_directories) / sizeof(font_directories[0])); i++) {
    if (popup_find_font_in_directory(font_directories[i], font_path, font_path_size)) {
      return true;
    }
  }

  return false;
}

static TTF_Font* popup_try_open_font(const char* path, int font_size) {
  if ((path == NULL) || (path[0] == '\0')) {
    return NULL;
  }

  if (access(path, R_OK) != 0) {
    return NULL;
  }

  return TTF_OpenFont(path, font_size);
}

static TTF_Font* popup_open_font(SDL_Renderer* renderer) {
  const char* preferred_fonts[] = {
    POPUP_FONT_FILENAME,
    "cour.ttf"};
  char fallback_font_path[256];

  if (TTF_WasInit() == 0 && TTF_Init() == -1) {
    printf("TTF_Init: %s\n", TTF_GetError());
    return NULL;
  }

  int font_size = popup_font_size_for_renderer(renderer);

  for (size_t i = 0; i < (sizeof(preferred_fonts) / sizeof(preferred_fonts[0])); i++) {
    TTF_Font* font = popup_try_open_font(preferred_fonts[i], font_size);

    if (font != NULL) {
      return font;
    }
  }

  fallback_font_path[0] = '\0';
  if (popup_find_font(fallback_font_path, sizeof(fallback_font_path))) {
    TTF_Font* font = popup_try_open_font(fallback_font_path, font_size);

    if (font != NULL) {
      return font;
    }
  }

  printf("Font file missing\r\n");
  return NULL;
}

static void popup_draw_frame(SDL_Renderer* renderer, const SDL_Rect* popup_rect) {
  SDL_Color bg_color = {242, 133, 0, 235};
  int min_side = popup_rect->w < popup_rect->h ? popup_rect->w : popup_rect->h;
  int border_thickness = min_side / 60;
  if (border_thickness < 2) {
    border_thickness = 2;
  } else if (border_thickness > 6) {
    border_thickness = 6;
  }

  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, bg_color.r, bg_color.g, bg_color.b, bg_color.a);
  SDL_RenderFillRect(renderer, popup_rect);

  SDL_Rect outer_border_rect = {
    popup_rect->x + 2 * border_thickness,
    popup_rect->y + 2 * border_thickness,
    popup_rect->w - 4 * border_thickness,
    popup_rect->h - 4 * border_thickness};
  if (outer_border_rect.w < 2) {
    outer_border_rect.w = 2;
  }
  if (outer_border_rect.h < 2) {
    outer_border_rect.h = 2;
  }
  SDL_Rect inner_border_rect = {
    outer_border_rect.x + 2 * border_thickness,
    outer_border_rect.y + 2 * border_thickness,
    outer_border_rect.w - 4 * border_thickness,
    outer_border_rect.h - 4 * border_thickness};
  if (inner_border_rect.w < 2) {
    inner_border_rect.w = 2;
  }
  if (inner_border_rect.h < 2) {
    inner_border_rect.h = 2;
  }

  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);

  for (int i = 0; i < border_thickness; i++) {
    outer_border_rect.x--;
    outer_border_rect.y--;
    outer_border_rect.w += 2;
    outer_border_rect.h += 2;
    SDL_RenderDrawRect(renderer, &outer_border_rect);

    inner_border_rect.x--;
    inner_border_rect.y--;
    inner_border_rect.w += 2;
    inner_border_rect.h += 2;
    SDL_RenderDrawRect(renderer, &inner_border_rect);
  }
}

static bool popup_point_in_rect(int x, int y, const SDL_Rect* rect) {
  return x >= rect->x && x < rect->x + rect->w &&
         y >= rect->y && y < rect->y + rect->h;
}

static int popup_button_width(TTF_Font* font, const char* label) {
  int text_width = 0;
  TTF_SizeUTF8(font, label, &text_width, NULL);
  int width = text_width + 32;
  return width < POPUP_BUTTON_MIN_WIDTH ? POPUP_BUTTON_MIN_WIDTH : width;
}

static void popup_button_rects(TTF_Font* font, const SDL_Rect* popup_rect,
                               int padding, const char* primary_label,
                               SDL_Rect* primary_rect,
                               SDL_Rect* cancel_rect) {
  int height = TTF_FontLineSkip(font) + 14;
  if (height < 34) {
    height = 34;
  }
  cancel_rect->w = popup_button_width(font, "Cancel");
  cancel_rect->h = height;
  cancel_rect->x = popup_rect->x + popup_rect->w - padding - cancel_rect->w;
  cancel_rect->y = popup_rect->y + popup_rect->h - padding - height;
  primary_rect->w = popup_button_width(font, primary_label);
  primary_rect->h = height;
  primary_rect->x = cancel_rect->x - POPUP_BUTTON_GAP - primary_rect->w;
  primary_rect->y = cancel_rect->y;
}

static void popup_draw_button(SDL_Renderer* renderer, TTF_Font* font,
                              const SDL_Rect* rect, const char* label,
                              bool highlighted) {
  SDL_Color text_color = {0, 0, 0, 255};
  if (highlighted) {
    SDL_SetRenderDrawColor(renderer, 255, 235, 170, 255);
  } else {
    SDL_SetRenderDrawColor(renderer, 232, 187, 105, 255);
  }
  SDL_RenderFillRect(renderer, rect);
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
  SDL_RenderDrawRect(renderer, rect);

  SDL_Surface* surface = TTF_RenderUTF8_Blended(font, label, text_color);
  SDL_Texture* texture = surface
    ? SDL_CreateTextureFromSurface(renderer, surface)
    : NULL;
  if (texture) {
    SDL_Rect text_rect = {
      rect->x + (rect->w - surface->w) / 2,
      rect->y + (rect->h - surface->h) / 2,
      surface->w, surface->h};
    SDL_RenderCopy(renderer, texture, NULL, &text_rect);
    SDL_DestroyTexture(texture);
  }
  if (surface) {
    SDL_FreeSurface(surface);
  }
}

static int popup_text_width(TTF_Font* font, const char* text) {
  int width = 0;
  TTF_SizeUTF8(font, text, &width, NULL);
  return width;
}

static size_t popup_utf8_character_size(const char* text) {
  unsigned char first = (unsigned char)text[0];
  if ((first & 0x80U) == 0U) {
    return 1;
  }
  if ((first & 0xe0U) == 0xc0U) {
    return 2;
  }
  if ((first & 0xf0U) == 0xe0U) {
    return 3;
  }
  if ((first & 0xf8U) == 0xf0U) {
    return 4;
  }
  return 1;
}

static void popup_fit_path(TTF_Font* font, const char* path, int max_width,
                           char* output, size_t output_size) {
  if (output_size < 4) {
    if (output_size > 0) {
      output[0] = '\0';
    }
    return;
  }
  snprintf(output, output_size, "%s", path);
  if (popup_text_width(font, output) <= max_width) {
    return;
  }

  const char* final_component = strrchr(path, '/');
  if (!final_component) {
    final_component = path;
  }

  size_t used = 0;
  const char* component = path;
  if (*component == '/' && used + 1 < output_size) {
    output[used++] = '/';
    component++;
  }
  while (*component && component < final_component) {
    const char* separator = strchr(component, '/');
    if (!separator || separator > final_component) {
      break;
    }
    size_t character_size = popup_utf8_character_size(component);
    if (used + character_size + 1 >= output_size) {
      break;
    }
    memcpy(output + used, component, character_size);
    used += character_size;
    output[used++] = '/';
    component = separator + 1;
  }
  snprintf(output + used, output_size - used, "%s",
           *final_component == '/' ? final_component + 1 : final_component);
  if (popup_text_width(font, output) <= max_width) {
    return;
  }

  const char* suffix = final_component;
  do {
    while (strlen(suffix) + 4 > output_size && *suffix) {
      suffix += popup_utf8_character_size(suffix);
    }
    size_t suffix_length = strlen(suffix);
    memcpy(output, "...", 3);
    memcpy(output + 3, suffix, suffix_length + 1);
    if (popup_text_width(font, output) <= max_width || *suffix == '\0') {
      return;
    }
    suffix += popup_utf8_character_size(suffix);
  } while (*suffix);
  snprintf(output, output_size, "...");
}

void popup_show(SDL_Renderer* renderer, const char* message) {
  SDL_Color text_color = {0, 0, 0, 255};
  SDL_Rect popup_rect = popup_center_rect(renderer, 420, 300, 12);

  TTF_Font* font = popup_open_font(renderer);
  if (!font) {
    return;
  }

  char* message_copy = strdup(message);
  if (!message_copy) {
    TTF_CloseFont(font);
    return;
  }

  char* lines[POPUP_MAX_LINES];
  int num_lines = 0;
  char* line = strtok(message_copy, "\r\n");

  while (line && num_lines < POPUP_MAX_LINES) {
    lines[num_lines++] = line;
    line = strtok(NULL, "\r\n");
  }

  SDL_Texture* text_textures[POPUP_MAX_LINES];
  int text_w[POPUP_MAX_LINES];
  int text_h[POPUP_MAX_LINES];
  int max_text_width = 0;
  int total_text_height = 0;

  for (int i = 0; i < num_lines; i++) {
    text_textures[i] = NULL;
    text_w[i] = 0;
    text_h[i] = 0;

    SDL_Surface* text_surface = TTF_RenderText_Blended(font, lines[i], text_color);
    if (!text_surface) {
      continue;
    }

    text_textures[i] = SDL_CreateTextureFromSurface(renderer, text_surface);
    text_w[i] = text_surface->w;
    text_h[i] = text_surface->h;

    if (text_surface->w > max_text_width) {
      max_text_width = text_surface->w;
    }
    total_text_height += text_surface->h + 5;

    SDL_FreeSurface(text_surface);
  }

  int desired_width = max_text_width + 80;
  int desired_height = total_text_height + 70;
  if (desired_width < 320) {
    desired_width = 320;
  }
  if (desired_height < 180) {
    desired_height = 180;
  }
  desired_height += 42;
  popup_rect = popup_center_rect(renderer, desired_width, desired_height, 12);
  int button_width = popup_button_width(font, "OK");
  int button_height = TTF_FontLineSkip(font) + 14;
  SDL_Rect ok_rect = {popup_rect.x + (popup_rect.w - button_width) / 2,
                      popup_rect.y + popup_rect.h - button_height - 28,
                      button_width, button_height};

  bool popup_done = false;

  while (!popup_done) {
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT) {
        popup_done = true;
      } else if (event.type == SDL_KEYDOWN) {
        popup_done = true;
      } else if (event.type == SDL_MOUSEBUTTONDOWN &&
                 event.button.button == SDL_BUTTON_LEFT &&
                 popup_point_in_rect(event.button.x, event.button.y,
                                     &ok_rect)) {
        popup_done = true;
      }
    }

    popup_draw_frame(renderer, &popup_rect);

    int y = popup_rect.y + 30;
    for (int i = 0; i < num_lines; i++) {
      if (!text_textures[i]) {
        continue;
      }
      SDL_Rect text_rect = {popup_rect.x + (popup_rect.w - text_w[i]) / 2, y, text_w[i], text_h[i]};
      SDL_RenderCopy(renderer, text_textures[i], NULL, &text_rect);
      y += text_h[i] + 5;
    }

    int mouse_x;
    int mouse_y;
    SDL_GetMouseState(&mouse_x, &mouse_y);
    popup_draw_button(renderer, font, &ok_rect, "OK",
                      popup_point_in_rect(mouse_x, mouse_y, &ok_rect));

    SDL_RenderPresent(renderer);
    SDL_Delay(16);
  }

  for (int i = 0; i < num_lines; i++) {
    if (text_textures[i]) {
      SDL_DestroyTexture(text_textures[i]);
    }
  }

  free(message_copy);
  TTF_CloseFont(font);
}

bool popup_prompt_input(SDL_Renderer* renderer, const char* title, const char* prompt, const char* default_value, char* output, size_t output_size) {
  if (!output || output_size == 0) {
    return false;
  }

  output[0] = '\0';

  if (default_value && *default_value) {
    snprintf(output, output_size, "%s", default_value);
  }

  SDL_Color text_color = {0, 0, 0, 255};
  SDL_Rect popup_rect = popup_center_rect(renderer, 560, 280, 12);
  int line_height = 28;
  int padding = 20;
  SDL_Rect input_rect = {0, 0, 0, 0};

  TTF_Font* font = popup_open_font(renderer);
  if (!font) {
    return false;
  }
  line_height = TTF_FontLineSkip(font);
  if (line_height < 18) {
    line_height = 18;
  }
  if (popup_rect.w < 420) {
    padding = 14;
  }
  SDL_Rect ok_rect;
  SDL_Rect cancel_rect;
  popup_button_rects(font, &popup_rect, padding, "OK", &ok_rect,
                     &cancel_rect);

  int input_h = (line_height < 22) ? 34 : 42;
  input_rect.x = popup_rect.x + padding;
  input_rect.w = popup_rect.w - (2 * padding);
  input_rect.h = input_h;
  input_rect.y = ok_rect.y - input_h - 16;
  if (input_rect.y < popup_rect.y + (2 * padding) + line_height * 2) {
    input_rect.y = popup_rect.y + (2 * padding) + line_height * 2;
  }

  bool done = false;
  bool accepted = false;
  uint32_t blink_start = SDL_GetTicks();

  SDL_StartTextInput();

  while (!done) {
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT) {
        done = true;
        accepted = false;
      } else if (event.type == SDL_KEYDOWN) {
        SDL_KeyCode key = event.key.keysym.sym;

        if (key == SDLK_ESCAPE) {
          done = true;
          accepted = false;
        } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
          done = true;
          accepted = output[0] != '\0';
        } else if (key == SDLK_BACKSPACE) {
          size_t length = strlen(output);
          if (length > 0) {
            output[length - 1] = '\0';
          }
        }
      } else if (event.type == SDL_TEXTINPUT) {
        const char* text = event.text.text;
        size_t current = strlen(output);
        size_t incoming = strlen(text);

        if (current + incoming < output_size) {
          strcat(output, text);
        }
      } else if (event.type == SDL_MOUSEBUTTONDOWN &&
                 event.button.button == SDL_BUTTON_LEFT) {
        if (popup_point_in_rect(event.button.x, event.button.y, &ok_rect)) {
          done = true;
          accepted = output[0] != '\0';
        } else if (popup_point_in_rect(event.button.x, event.button.y,
                                      &cancel_rect)) {
          done = true;
          accepted = false;
        }
      }
    }

    popup_draw_frame(renderer, &popup_rect);

    SDL_Surface* title_surface = TTF_RenderText_Blended(font, title, text_color);
    SDL_Surface* prompt_surface = TTF_RenderText_Blended(font, prompt, text_color);
    SDL_Surface* value_surface = TTF_RenderText_Blended(font, output, text_color);

    SDL_Texture* title_texture = title_surface ? SDL_CreateTextureFromSurface(renderer, title_surface) : NULL;
    SDL_Texture* prompt_texture = prompt_surface ? SDL_CreateTextureFromSurface(renderer, prompt_surface) : NULL;
    SDL_Texture* value_texture = value_surface ? SDL_CreateTextureFromSurface(renderer, value_surface) : NULL;

    if (title_texture) {
      SDL_Rect rect = {popup_rect.x + padding, popup_rect.y + padding, title_surface->w, title_surface->h};
      SDL_RenderCopy(renderer, title_texture, NULL, &rect);
    }

    if (prompt_texture) {
      SDL_Rect rect = {popup_rect.x + padding, popup_rect.y + padding + line_height + 8, prompt_surface->w, prompt_surface->h};
      SDL_RenderCopy(renderer, prompt_texture, NULL, &rect);
    }

    SDL_SetRenderDrawColor(renderer, 255, 235, 170, 255);
    SDL_RenderFillRect(renderer, &input_rect);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderDrawRect(renderer, &input_rect);

    if (value_texture) {
      SDL_Rect rect = {input_rect.x + 8, input_rect.y + (input_rect.h - value_surface->h) / 2, value_surface->w, value_surface->h};
      SDL_RenderCopy(renderer, value_texture, NULL, &rect);

      uint32_t elapsed = SDL_GetTicks() - blink_start;
      if (((elapsed / 500U) % 2U) == 0U) {
        int cursor_x = rect.x + rect.w + 2;
        SDL_RenderDrawLine(renderer, cursor_x, input_rect.y + 7, cursor_x, input_rect.y + input_rect.h - 7);
      }
    }

    int mouse_x;
    int mouse_y;
    SDL_GetMouseState(&mouse_x, &mouse_y);
    popup_draw_button(renderer, font, &ok_rect, "OK",
                      popup_point_in_rect(mouse_x, mouse_y, &ok_rect));
    popup_draw_button(renderer, font, &cancel_rect, "Cancel",
                      popup_point_in_rect(mouse_x, mouse_y, &cancel_rect));

    SDL_RenderPresent(renderer);
    SDL_Delay(16);

    if (title_texture)
      SDL_DestroyTexture(title_texture);
    if (prompt_texture)
      SDL_DestroyTexture(prompt_texture);
    if (value_texture)
      SDL_DestroyTexture(value_texture);

    if (title_surface)
      SDL_FreeSurface(title_surface);
    if (prompt_surface)
      SDL_FreeSurface(prompt_surface);
    if (value_surface)
      SDL_FreeSurface(value_surface);
  }

  SDL_StopTextInput();
  TTF_CloseFont(font);
  return accepted;
}
typedef struct popup_file_entry_t {
  char name[PATH_MAX];
  bool is_dir;
} popup_file_entry_t;

static bool popup_name_has_extension(const char* file_name, const char* const* extensions, int extension_count) {
  if (!extensions || extension_count <= 0) {
    return true;
  }

  const char* dot = strrchr(file_name, '.');
  if (!dot) {
    return false;
  }

  for (int i = 0; i < extension_count; i++) {
    if (extensions[i] && strcasecmp(dot, extensions[i]) == 0) {
      return true;
    }
  }

  return false;
}

static int popup_file_entry_compare(const void* left_ptr, const void* right_ptr) {
  const popup_file_entry_t* left = (const popup_file_entry_t*)left_ptr;
  const popup_file_entry_t* right = (const popup_file_entry_t*)right_ptr;

  bool left_parent = strcmp(left->name, "..") == 0;
  bool right_parent = strcmp(right->name, "..") == 0;

  if (left_parent != right_parent) {
    return left_parent ? -1 : 1;
  }

  if (left->is_dir != right->is_dir) {
    return left->is_dir ? -1 : 1;
  }

  return strcasecmp(left->name, right->name);
}

static bool popup_join_path(char* output, size_t output_size, const char* directory, const char* name) {
  int written;

  if (strcmp(directory, "/") == 0) {
    written = snprintf(output, output_size, "/%s", name);
  } else {
    written = snprintf(output, output_size, "%s/%s", directory, name);
  }

  return written > 0 && (size_t)written < output_size;
}

static bool popup_is_directory_path(const char* path) {
  struct stat st;
  return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool popup_append_file_entry(popup_file_entry_t** entries, int* count, int* capacity, const char* name, bool is_dir) {
  if (*count >= *capacity) {
    int new_capacity = (*capacity == 0) ? 64 : (*capacity * 2);
    popup_file_entry_t* new_entries = (popup_file_entry_t*)realloc(*entries, (size_t)new_capacity * sizeof(popup_file_entry_t));

    if (!new_entries) {
      return false;
    }

    *entries = new_entries;
    *capacity = new_capacity;
  }

  popup_file_entry_t* entry = &(*entries)[*count];
  snprintf(entry->name, sizeof(entry->name), "%s", name);
  entry->is_dir = is_dir;
  (*count)++;
  return true;
}

static bool popup_list_directory(const char* directory,
                                 popup_file_entry_t** entries_out,
                                 int* count_out,
                                 const char* const* extensions,
                                 int extension_count) {
  DIR* dir = opendir(directory);
  if (!dir) {
    return false;
  }

  popup_file_entry_t* entries = NULL;
  int count = 0;
  int capacity = 0;

  if (strcmp(directory, "/") != 0) {
    if (!popup_append_file_entry(&entries, &count, &capacity, "..", true)) {
      closedir(dir);
      free(entries);
      return false;
    }
  }

  struct dirent* item;
  while ((item = readdir(dir)) != NULL) {
    if ((strcmp(item->d_name, ".") == 0) ||
        (strcmp(item->d_name, "..") == 0)) {
      continue;
    }

    char full_path[PATH_MAX];
    if (!popup_join_path(full_path, sizeof(full_path), directory, item->d_name)) {
      continue;
    }

    struct stat st;
    if (stat(full_path, &st) != 0) {
      continue;
    }

    bool is_dir = S_ISDIR(st.st_mode);

    if (!is_dir && !popup_name_has_extension(item->d_name, extensions, extension_count)) {
      continue;
    }

    if (!popup_append_file_entry(&entries, &count, &capacity, item->d_name, is_dir)) {
      closedir(dir);
      free(entries);
      return false;
    }
  }

  closedir(dir);

  if (count > 1) {
    qsort(entries, (size_t)count, sizeof(popup_file_entry_t), popup_file_entry_compare);
  }

  *entries_out = entries;
  *count_out = count;
  return true;
}

static bool popup_resolve_start_directory(const char* start_directory, char* output, size_t output_size) {
  if (start_directory && *start_directory && popup_is_directory_path(start_directory)) {
    char resolved[PATH_MAX];
    if (realpath(start_directory, resolved)) {
      snprintf(output, output_size, "%s", resolved);
      return true;
    }

    snprintf(output, output_size, "%s", start_directory);
    return true;
  }

  if (getcwd(output, output_size) != NULL) {
    return true;
  }

  snprintf(output, output_size, "/");
  return true;
}

bool popup_file_select(SDL_Renderer* renderer,
                       const char* title,
                       const char* start_directory,
                       const char* const* extensions,
                       int extension_count,
                       bool allow_text_entry,
                       const char* default_name,
                       char* output,
                       size_t output_size) {
  if (!output || output_size == 0) {
    return false;
  }

  output[0] = '\0';

  SDL_Color text_color = {0, 0, 0, 255};
  SDL_Rect popup_rect = popup_center_rect(renderer, 700, 540, 10);
  SDL_Rect list_rect = {0, 0, 0, 0};
  SDL_Rect input_rect = {0, 0, 0, 0};
  int line_height;
  int row_height;
  int padding = (popup_rect.w < 540) ? 12 : 20;

  TTF_Font* font = popup_open_font(renderer);
  if (!font) {
    return false;
  }
  line_height = TTF_FontLineSkip(font);
  if (line_height < 16) {
    line_height = 16;
  }
  row_height = line_height + 10;
  if (row_height < 24) {
    row_height = 24;
  }

  const char* primary_label = allow_text_entry ? "Save" : "Open";
  SDL_Rect primary_rect;
  SDL_Rect cancel_rect;
  int button_padding = padding + 10;
  popup_button_rects(font, &popup_rect, button_padding, primary_label,
                     &primary_rect, &cancel_rect);

  int header_height = padding + line_height * 2 + 16;
  int content_padding = padding + 10;
  list_rect.x = popup_rect.x + content_padding;
  list_rect.y = popup_rect.y + header_height;
  list_rect.w = popup_rect.w - (2 * content_padding);

  input_rect.x = popup_rect.x + content_padding;
  input_rect.w = popup_rect.w - (2 * content_padding);
  input_rect.h = (line_height < 22) ? 34 : 42;
  input_rect.y = primary_rect.y - input_rect.h - line_height - 14;
  list_rect.h = (allow_text_entry ? input_rect.y - line_height - 12
                                  : primary_rect.y - 12) - list_rect.y;
  if (list_rect.h < row_height) {
    list_rect.h = row_height;
  }

  char current_directory[PATH_MAX];
  popup_resolve_start_directory(start_directory, current_directory, sizeof(current_directory));

  char typed_name[PATH_MAX];
  typed_name[0] = '\0';
  if (allow_text_entry && default_name && *default_name) {
    snprintf(typed_name, sizeof(typed_name), "%s", default_name);
  }

  popup_file_entry_t* entries = NULL;
  int entry_count = 0;
  int selected_index = 0;
  int hovered_index = -1;
  int scroll_offset = 0;
  bool refresh_entries = true;
  bool done = false;
  bool accepted = false;
  uint32_t blink_start = SDL_GetTicks();

  if (allow_text_entry) {
    SDL_StartTextInput();
  }

  while (!done) {
    if (refresh_entries) {
      free(entries);
      entries = NULL;
      entry_count = 0;

      if (!popup_list_directory(current_directory, &entries, &entry_count, extensions, extension_count)) {
        popup_show(renderer, "Unable to open directory.");
        break;
      }

      if (selected_index >= entry_count) {
        selected_index = (entry_count > 0) ? (entry_count - 1) : 0;
      }
      if (selected_index < 0) {
        selected_index = 0;
      }
      hovered_index = -1;

      refresh_entries = false;
    }

    bool activate_selection = false;
    bool activate_selected_entry = false;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT) {
        done = true;
        accepted = false;
      } else if (event.type == SDL_MOUSEWHEEL) {
        hovered_index = -1;
        if (event.wheel.y > 0 && selected_index > 0) {
          selected_index--;
        } else if (event.wheel.y < 0 && selected_index + 1 < entry_count) {
          selected_index++;
        }
      } else if (event.type == SDL_TEXTINPUT && allow_text_entry) {
        size_t current_length = strlen(typed_name);
        size_t incoming_length = strlen(event.text.text);

        if (current_length + incoming_length < sizeof(typed_name)) {
          strcat(typed_name, event.text.text);
        }
      } else if (event.type == SDL_MOUSEMOTION) {
        hovered_index = -1;
        if (popup_point_in_rect(event.motion.x, event.motion.y,
                                &list_rect)) {
          int row = (event.motion.y - list_rect.y - 4) / row_height;
          int candidate = scroll_offset + row;
          if (candidate >= 0 && candidate < entry_count) {
            hovered_index = candidate;
          }
        }
      } else if (event.type == SDL_MOUSEBUTTONDOWN &&
                 event.button.button == SDL_BUTTON_LEFT) {
        if (popup_point_in_rect(event.button.x, event.button.y,
                                &cancel_rect)) {
          done = true;
          accepted = false;
        } else if (popup_point_in_rect(event.button.x, event.button.y,
                                       &primary_rect)) {
          activate_selection = true;
        } else if (popup_point_in_rect(event.button.x, event.button.y,
                                       &list_rect)) {
          int row = (event.button.y - list_rect.y - 4) / row_height;
          int clicked_index = scroll_offset + row;
          if (clicked_index >= 0 && clicked_index < entry_count) {
            selected_index = clicked_index;
            if (allow_text_entry && !entries[selected_index].is_dir) {
              snprintf(typed_name, sizeof(typed_name), "%s",
                       entries[selected_index].name);
            }
            if (event.button.clicks >= 2) {
              activate_selection = true;
              activate_selected_entry = true;
            }
          }
        }
      } else if (event.type == SDL_KEYDOWN) {
        SDL_KeyCode key = event.key.keysym.sym;
        int visible_rows = list_rect.h / row_height;
        if (visible_rows < 1) {
          visible_rows = 1;
        }

        if (key == SDLK_ESCAPE) {
          done = true;
          accepted = false;
        } else if (key == SDLK_UP) {
          if (selected_index > 0) {
            selected_index--;
          }
        } else if (key == SDLK_DOWN) {
          if (selected_index + 1 < entry_count) {
            selected_index++;
          }
        } else if (key == SDLK_PAGEUP) {
          selected_index -= visible_rows;
          if (selected_index < 0) {
            selected_index = 0;
          }
        } else if (key == SDLK_PAGEDOWN) {
          selected_index += visible_rows;
          if (selected_index >= entry_count) {
            selected_index = (entry_count > 0) ? (entry_count - 1) : 0;
          }
        } else if (key == SDLK_HOME) {
          selected_index = 0;
        } else if (key == SDLK_END) {
          selected_index = (entry_count > 0) ? (entry_count - 1) : 0;
        } else if (key == SDLK_LEFT) {
          char parent_directory[PATH_MAX];
          if (popup_join_path(parent_directory, sizeof(parent_directory), current_directory, "..")) {
            char resolved[PATH_MAX];
            if (realpath(parent_directory, resolved) && popup_is_directory_path(resolved)) {
              snprintf(current_directory, sizeof(current_directory), "%s", resolved);
              selected_index = 0;
              scroll_offset = 0;
              refresh_entries = true;
            }
          }
        } else if (allow_text_entry && key == SDLK_BACKSPACE) {
          size_t length = strlen(typed_name);
          if (length > 0) {
            typed_name[length - 1] = '\0';
          }
        } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
          activate_selection = true;
        }
      }
    }

    if (!done && activate_selection) {
      popup_file_entry_t* selected = entry_count > 0
        ? &entries[selected_index] : NULL;
      if (activate_selected_entry && selected && selected->is_dir) {
        char next_directory[PATH_MAX];
        if (popup_join_path(next_directory, sizeof(next_directory),
                            current_directory, selected->name)) {
          char resolved[PATH_MAX];
          if (realpath(next_directory, resolved) &&
              popup_is_directory_path(resolved)) {
            snprintf(current_directory, sizeof(current_directory), "%s",
                     resolved);
            selected_index = 0;
            scroll_offset = 0;
            refresh_entries = true;
          }
        }
      } else if (allow_text_entry && typed_name[0] != '\0') {
        if (popup_join_path(output, output_size, current_directory,
                            typed_name)) {
          accepted = true;
          done = true;
        }
      } else if (selected) {
        if (selected->is_dir) {
          char next_directory[PATH_MAX];
          if (popup_join_path(next_directory, sizeof(next_directory),
                              current_directory, selected->name)) {
            char resolved[PATH_MAX];
            if (realpath(next_directory, resolved) &&
                popup_is_directory_path(resolved)) {
              snprintf(current_directory, sizeof(current_directory), "%s",
                       resolved);
              selected_index = 0;
              scroll_offset = 0;
              refresh_entries = true;
            }
          }
        } else if (popup_join_path(output, output_size, current_directory,
                                   selected->name)) {
          accepted = true;
          done = true;
        }
      }
    }

    int visible_rows = list_rect.h / row_height;
    if (visible_rows < 1) {
      visible_rows = 1;
    }

    if (selected_index < scroll_offset) {
      scroll_offset = selected_index;
    }
    if (selected_index >= scroll_offset + visible_rows) {
      scroll_offset = selected_index - visible_rows + 1;
    }

    popup_draw_frame(renderer, &popup_rect);

    int header_inset = 8;
    int header_x = popup_rect.x + padding + header_inset;
    int header_y = popup_rect.y + padding + 4;
    int header_width = popup_rect.x + popup_rect.w - padding - header_inset -
                       header_x;
    char displayed_path[PATH_MAX];
    popup_fit_path(font, current_directory, header_width, displayed_path,
                   sizeof(displayed_path));
    SDL_Surface* title_surface = TTF_RenderText_Blended(font, title, text_color);
    SDL_Surface* path_surface = TTF_RenderText_Blended(font, displayed_path,
                                                        text_color);

    SDL_Texture* title_texture = title_surface ? SDL_CreateTextureFromSurface(renderer, title_surface) : NULL;
    SDL_Texture* path_texture = path_surface ? SDL_CreateTextureFromSurface(renderer, path_surface) : NULL;

    if (title_texture) {
      SDL_Rect rect = {header_x, header_y,
                       title_surface->w, title_surface->h};
      SDL_RenderCopy(renderer, title_texture, NULL, &rect);
    }

    if (path_texture) {
      SDL_Rect rect = {header_x, header_y + line_height + 4,
                       path_surface->w, path_surface->h};
      SDL_RenderCopy(renderer, path_texture, NULL, &rect);
    }

    SDL_SetRenderDrawColor(renderer, 255, 235, 170, 255);
    SDL_RenderFillRect(renderer, &list_rect);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderDrawRect(renderer, &list_rect);

    int y = list_rect.y + 6;
    int max_row = scroll_offset + visible_rows;
    if (max_row > entry_count) {
      max_row = entry_count;
    }

    for (int i = scroll_offset; i < max_row; i++) {
      SDL_Rect row_rect = {list_rect.x + 6, y - 2, list_rect.w - 12, row_height - 4};
      bool hovered = i == hovered_index;
      if (hovered || i == selected_index) {
        SDL_SetRenderDrawColor(renderer, 255, 210, 120, 255);
        SDL_RenderFillRect(renderer, &row_rect);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderDrawRect(renderer, &row_rect);
      }

      char line[PATH_MAX + 8];
      snprintf(line, sizeof(line), "%s%s", entries[i].is_dir ? "[D] " : "    ", entries[i].name);
      SDL_Surface* row_surface = TTF_RenderText_Blended(font, line,
                                                        text_color);
      SDL_Texture* row_texture = row_surface ? SDL_CreateTextureFromSurface(renderer, row_surface) : NULL;

      if (row_texture) {
        SDL_Rect rect = {row_rect.x + 8, row_rect.y + (row_rect.h - row_surface->h) / 2, row_surface->w, row_surface->h};
        SDL_RenderCopy(renderer, row_texture, NULL, &rect);
      }

      if (row_texture)
        SDL_DestroyTexture(row_texture);
      if (row_surface)
        SDL_FreeSurface(row_surface);

      y += row_height;
    }

    if (allow_text_entry) {
      SDL_SetRenderDrawColor(renderer, 255, 235, 170, 255);
      SDL_RenderFillRect(renderer, &input_rect);
      SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
      SDL_RenderDrawRect(renderer, &input_rect);

      SDL_Surface* label_surface = TTF_RenderText_Blended(font, "File name:", text_color);
      SDL_Surface* value_surface = TTF_RenderText_Blended(font, typed_name, text_color);
      SDL_Texture* label_texture = label_surface ? SDL_CreateTextureFromSurface(renderer, label_surface) : NULL;
      SDL_Texture* value_texture = value_surface ? SDL_CreateTextureFromSurface(renderer, value_surface) : NULL;

      if (label_texture) {
        SDL_Rect rect = {input_rect.x + 6, input_rect.y - label_surface->h - 6, label_surface->w, label_surface->h};
        SDL_RenderCopy(renderer, label_texture, NULL, &rect);
      }

      if (value_texture) {
        SDL_Rect rect = {input_rect.x + 8, input_rect.y + (input_rect.h - value_surface->h) / 2, value_surface->w, value_surface->h};
        SDL_RenderCopy(renderer, value_texture, NULL, &rect);

        uint32_t elapsed = SDL_GetTicks() - blink_start;
        if (((elapsed / 500U) % 2U) == 0U) {
          int cursor_x = rect.x + rect.w + 2;
          SDL_RenderDrawLine(renderer, cursor_x, input_rect.y + 7, cursor_x, input_rect.y + input_rect.h - 7);
        }
      }

      if (label_texture)
        SDL_DestroyTexture(label_texture);
      if (value_texture)
        SDL_DestroyTexture(value_texture);
      if (label_surface)
        SDL_FreeSurface(label_surface);
      if (value_surface)
        SDL_FreeSurface(value_surface);
    }

    int mouse_x;
    int mouse_y;
    SDL_GetMouseState(&mouse_x, &mouse_y);
    popup_draw_button(renderer, font, &primary_rect, primary_label,
                      popup_point_in_rect(mouse_x, mouse_y, &primary_rect));
    popup_draw_button(renderer, font, &cancel_rect, "Cancel",
                      popup_point_in_rect(mouse_x, mouse_y, &cancel_rect));

    SDL_RenderPresent(renderer);
    SDL_Delay(16);

    if (title_texture)
      SDL_DestroyTexture(title_texture);
    if (path_texture)
      SDL_DestroyTexture(path_texture);
    if (title_surface)
      SDL_FreeSurface(title_surface);
    if (path_surface)
      SDL_FreeSurface(path_surface);
  }

  if (allow_text_entry) {
    SDL_StopTextInput();
  }

  free(entries);
  TTF_CloseFont(font);
  return accepted;
}
int popup_menu_select(SDL_Renderer* renderer, const char* title, const char* const* items, int item_count, int selected_index) {
  if (!items || item_count <= 0 || item_count > POPUP_MAX_ITEMS) {
    return -1;
  }

  SDL_Color text_color = {0, 0, 0, 255};
  SDL_Rect popup_rect = popup_center_rect(renderer, 520, 420, 12);

  TTF_Font* font = popup_open_font(renderer);
  if (!font) {
    return -1;
  }

  SDL_Surface* title_surface = TTF_RenderText_Blended(font, title, text_color);
  SDL_Texture* title_texture = NULL;
  int title_w = 0;
  int title_h = 0;

  if (title_surface) {
    title_texture = SDL_CreateTextureFromSurface(renderer, title_surface);
    title_w = title_surface->w;
    title_h = title_surface->h;
    SDL_FreeSurface(title_surface);
  }

  SDL_Texture* item_textures[POPUP_MAX_ITEMS];
  int item_w[POPUP_MAX_ITEMS];
  int item_h[POPUP_MAX_ITEMS];
  int max_text_width = title_w;

  for (int i = 0; i < item_count; i++) {
    item_textures[i] = NULL;
    item_w[i] = 0;
    item_h[i] = 0;

    SDL_Surface* item_surface = TTF_RenderText_Blended(font, items[i], text_color);
    if (!item_surface) {
      continue;
    }

    item_textures[i] = SDL_CreateTextureFromSurface(renderer, item_surface);
    item_w[i] = item_surface->w;
    item_h[i] = item_surface->h;

    if (item_surface->w > max_text_width) {
      max_text_width = item_surface->w;
    }

    SDL_FreeSurface(item_surface);
  }

  if (selected_index < 0 || selected_index >= item_count) {
    selected_index = 0;
  }

  int content_height = title_h + 24;
  for (int i = 0; i < item_count; i++) {
    content_height += item_h[i] + 12;
  }

  popup_rect.w = max_text_width + 120;
  if (popup_rect.w < 420) {
    popup_rect.w = 420;
  }

  popup_rect.h = content_height + 94;
  if (popup_rect.h < 260) {
    popup_rect.h = 260;
  }
  popup_rect = popup_center_rect(renderer, popup_rect.w, popup_rect.h, 12);
  SDL_Rect select_rect;
  SDL_Rect cancel_rect;
  popup_button_rects(font, &popup_rect, 20, "Select", &select_rect,
                     &cancel_rect);

  bool menu_done = false;
  int menu_result = -1;

  while (!menu_done) {
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT) {
        menu_done = true;
        menu_result = -1;
      } else if (event.type == SDL_KEYDOWN) {
        SDL_KeyCode key = event.key.keysym.sym;

        if (key == SDLK_UP) {
          selected_index--;
          if (selected_index < 0) {
            selected_index = item_count - 1;
          }
        } else if (key == SDLK_DOWN) {
          selected_index++;
          if (selected_index >= item_count) {
            selected_index = 0;
          }
        } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
          menu_result = selected_index;
          menu_done = true;
        } else if (key == SDLK_ESCAPE) {
          menu_result = -1;
          menu_done = true;
        }
      } else if ((event.type == SDL_MOUSEMOTION) ||
                 (event.type == SDL_MOUSEBUTTONDOWN &&
                  event.button.button == SDL_BUTTON_LEFT)) {
        int mouse_x = event.type == SDL_MOUSEMOTION
          ? event.motion.x : event.button.x;
        int mouse_y = event.type == SDL_MOUSEMOTION
          ? event.motion.y : event.button.y;
        int row_y = popup_rect.y + 20;
        if (title_texture) {
          row_y += title_h + 20;
        }
        for (int i = 0; i < item_count; i++) {
          SDL_Rect row_rect = {popup_rect.x + 20, row_y - 4,
                               popup_rect.w - 40, item_h[i] + 8};
          if (popup_point_in_rect(mouse_x, mouse_y, &row_rect)) {
            selected_index = i;
            if (event.type == SDL_MOUSEBUTTONDOWN &&
                event.button.clicks >= 2) {
              menu_result = selected_index;
              menu_done = true;
            }
            break;
          }
          row_y += item_h[i] + 12;
        }

        if (event.type == SDL_MOUSEBUTTONDOWN) {
          if (popup_point_in_rect(mouse_x, mouse_y, &select_rect)) {
            menu_result = selected_index;
            menu_done = true;
          } else if (popup_point_in_rect(mouse_x, mouse_y, &cancel_rect)) {
            menu_result = -1;
            menu_done = true;
          }
        }
      }
    }

    popup_draw_frame(renderer, &popup_rect);

    int y = popup_rect.y + 20;

    if (title_texture) {
      SDL_Rect title_rect = {popup_rect.x + (popup_rect.w - title_w) / 2, y, title_w, title_h};
      SDL_RenderCopy(renderer, title_texture, NULL, &title_rect);
      y += title_h + 20;
    }

    for (int i = 0; i < item_count; i++) {
      SDL_Rect row_rect = {popup_rect.x + 20, y - 4, popup_rect.w - 40, item_h[i] + 8};

      if (i == selected_index) {
        SDL_SetRenderDrawColor(renderer, 255, 235, 170, 255);
        SDL_RenderFillRect(renderer, &row_rect);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderDrawRect(renderer, &row_rect);
      }

      if (item_textures[i]) {
        SDL_Rect text_rect = {popup_rect.x + 35, y, item_w[i], item_h[i]};
        SDL_RenderCopy(renderer, item_textures[i], NULL, &text_rect);
      }

      y += item_h[i] + 12;
    }

    int mouse_x;
    int mouse_y;
    SDL_GetMouseState(&mouse_x, &mouse_y);
    popup_draw_button(renderer, font, &select_rect, "Select",
                      popup_point_in_rect(mouse_x, mouse_y, &select_rect));
    popup_draw_button(renderer, font, &cancel_rect, "Cancel",
                      popup_point_in_rect(mouse_x, mouse_y, &cancel_rect));

    SDL_RenderPresent(renderer);
    SDL_Delay(16);
  }

  for (int i = 0; i < item_count; i++) {
    if (item_textures[i]) {
      SDL_DestroyTexture(item_textures[i]);
    }
  }

  if (title_texture) {
    SDL_DestroyTexture(title_texture);
  }

  TTF_CloseFont(font);
  return menu_result;
}
