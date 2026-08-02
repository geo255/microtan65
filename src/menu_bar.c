#include "menu_bar.h"

#include "external_filenames.h"

#include <stddef.h>

#define MENU_FONT_SIZE       16
#define MENU_TOP_PADDING     12
#define MENU_ITEM_HEIGHT     26
#define MENU_SEPARATOR_HEIGHT 9
#define MENU_DROP_PADDING    8
#define MENU_CHECK_WIDTH     20
#define MENU_SHORTCUT_GAP    36
#define MENU_MIN_WIDTH       180

static const SDL_Color MENU_TEXT = {25, 30, 28, 255};
static const SDL_Color MENU_DISABLED = {125, 125, 118, 255};
static const SDL_Color MENU_BAR_BACKGROUND = {242, 133, 0, 255};
static const SDL_Color MENU_DROP_BACKGROUND = {255, 180, 64, 255};
static const SDL_Color MENU_HIGHLIGHT = {166, 74, 0, 255};
static const SDL_Color MENU_HIGHLIGHT_TEXT = {255, 255, 245, 255};
static const SDL_Color MENU_BORDER = {55, 61, 57, 255};

static bool item_is_separator(const menu_bar_item_t* item) {
  return item->command == MENU_BAR_SEPARATOR_COMMAND;
}

static int text_width(TTF_Font* font, const char* text) {
  int width = 0;
  if (font && text) {
    TTF_SizeUTF8(font, text, &width, NULL);
  }
  return width;
}

static void draw_text(SDL_Renderer* renderer, TTF_Font* font,
                      const char* text, SDL_Color colour, int x, int y) {
  if (!renderer || !font || !text || !*text) {
    return;
  }

  SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text, colour);
  if (!surface) {
    return;
  }

  SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
  if (texture) {
    SDL_Rect destination = {x, y, surface->w, surface->h};
    SDL_RenderCopy(renderer, texture, NULL, &destination);
    SDL_DestroyTexture(texture);
  }
  SDL_FreeSurface(surface);
}

static int top_menu_width(TTF_Font* font, const menu_bar_menu_t* menu) {
  return text_width(font, menu->label) + MENU_TOP_PADDING * 2;
}

static int top_menu_x(TTF_Font* font, const menu_bar_menu_t* menus, int menu_index) {
  int x = 0;
  for (int index = 0; index < menu_index; index++) {
    x += top_menu_width(font, &menus[index]);
  }
  return x;
}

static int menu_at_x(TTF_Font* font, const menu_bar_menu_t* menus,
                     int menu_count, int x) {
  int left = 0;
  for (int index = 0; index < menu_count; index++) {
    int right = left + top_menu_width(font, &menus[index]);
    if ((x >= left) && (x < right)) {
      return index;
    }
    left = right;
  }
  return -1;
}

static int dropdown_width(TTF_Font* font, const menu_bar_menu_t* menu) {
  int width = MENU_MIN_WIDTH;
  for (int index = 0; index < menu->item_count; index++) {
    const menu_bar_item_t* item = &menu->items[index];
    if (item_is_separator(item)) {
      continue;
    }
    int item_width = MENU_DROP_PADDING * 2 + MENU_CHECK_WIDTH +
                     text_width(font, item->label);
    if (item->shortcut && *item->shortcut) {
      item_width += MENU_SHORTCUT_GAP + text_width(font, item->shortcut);
    }
    if (item_width > width) {
      width = item_width;
    }
  }
  return width;
}

static int dropdown_height(const menu_bar_menu_t* menu) {
  int height = MENU_DROP_PADDING * 2;
  for (int index = 0; index < menu->item_count; index++) {
    height += item_is_separator(&menu->items[index])
      ? MENU_SEPARATOR_HEIGHT
      : MENU_ITEM_HEIGHT;
  }
  return height;
}

static SDL_Rect dropdown_rect(SDL_Renderer* renderer, TTF_Font* font,
                              const menu_bar_menu_t* menus, int menu_index) {
  int renderer_width = 0;
  SDL_GetRendererOutputSize(renderer, &renderer_width, NULL);
  int width = dropdown_width(font, &menus[menu_index]);
  int x = top_menu_x(font, menus, menu_index);
  if (x + width > renderer_width) {
    x = renderer_width - width;
  }
  if (x < 0) {
    x = 0;
  }
  return (SDL_Rect){x, MENU_BAR_HEIGHT, width,
                    dropdown_height(&menus[menu_index])};
}

static SDL_Rect item_rect(const SDL_Rect* dropdown,
                          const menu_bar_menu_t* menu, int item_index) {
  int y = dropdown->y + MENU_DROP_PADDING;
  for (int index = 0; index < item_index; index++) {
    y += item_is_separator(&menu->items[index])
      ? MENU_SEPARATOR_HEIGHT
      : MENU_ITEM_HEIGHT;
  }
  int height = item_is_separator(&menu->items[item_index])
    ? MENU_SEPARATOR_HEIGHT
    : MENU_ITEM_HEIGHT;
  return (SDL_Rect){dropdown->x + 2, y, dropdown->w - 4, height};
}

static int item_at_point(const SDL_Rect* dropdown,
                         const menu_bar_menu_t* menu, int x, int y) {
  if ((x < dropdown->x) || (x >= dropdown->x + dropdown->w) ||
      (y < dropdown->y) || (y >= dropdown->y + dropdown->h)) {
    return -1;
  }

  for (int index = 0; index < menu->item_count; index++) {
    SDL_Rect row = item_rect(dropdown, menu, index);
    if ((y >= row.y) && (y < row.y + row.h)) {
      return item_is_separator(&menu->items[index]) ? -1 : index;
    }
  }
  return -1;
}

static int next_enabled_item(const menu_bar_menu_t* menu, int current,
                             int direction) {
  if (menu->item_count <= 0) {
    return -1;
  }

  int index = current;
  for (int count = 0; count < menu->item_count; count++) {
    index += direction;
    if (index < 0) {
      index = menu->item_count - 1;
    } else if (index >= menu->item_count) {
      index = 0;
    }
    const menu_bar_item_t* item = &menu->items[index];
    if (!item_is_separator(item) && item->enabled) {
      return index;
    }
  }
  return -1;
}

static void open_menu(menu_bar_state_t* state,
                      const menu_bar_menu_t* menus, int menu_index) {
  state->active_menu = menu_index;
  state->hot_item = next_enabled_item(&menus[menu_index], -1, 1);
}

bool menu_bar_initialise(menu_bar_state_t* state) {
  if (!state) {
    return false;
  }
  state->font = NULL;
  state->active_menu = -1;
  state->hot_item = -1;

  if ((TTF_WasInit() == 0) && (TTF_Init() != 0)) {
    return false;
  }
  state->font = TTF_OpenFont(POPUP_FONT_FILENAME, MENU_FONT_SIZE);
  return state->font != NULL;
}

void menu_bar_close(menu_bar_state_t* state) {
  if (state && state->font) {
    TTF_CloseFont(state->font);
    state->font = NULL;
  }
}

bool menu_bar_handle_event(menu_bar_state_t* state,
                           SDL_Renderer* renderer,
                           const SDL_Event* event,
                           const menu_bar_menu_t* menus,
                           int menu_count, int* command) {
  if (!state || !event || !menus || (menu_count <= 0)) {
    return false;
  }
  if (command) {
    *command = MENU_BAR_SEPARATOR_COMMAND;
  }

  if ((event->type == SDL_KEYDOWN) &&
      (event->key.keysym.sym == SDLK_F1)) {
    if (state->active_menu >= 0) {
      state->active_menu = -1;
      state->hot_item = -1;
    } else {
      open_menu(state, menus, 0);
    }
    return true;
  }

  if (event->type == SDL_MOUSEBUTTONDOWN &&
      event->button.button == SDL_BUTTON_LEFT) {
    if (event->button.y < MENU_BAR_HEIGHT) {
      int menu_index = menu_at_x(state->font, menus, menu_count,
                                 event->button.x);
      if (menu_index >= 0) {
        if (state->active_menu == menu_index) {
          state->active_menu = -1;
          state->hot_item = -1;
        } else {
          open_menu(state, menus, menu_index);
        }
      } else {
        state->active_menu = -1;
        state->hot_item = -1;
      }
      return true;
    }

    if (state->active_menu >= 0) {
      const menu_bar_menu_t* menu = &menus[state->active_menu];
      SDL_Rect dropdown = dropdown_rect(renderer, state->font, menus,
                                        state->active_menu);
      int item_index = item_at_point(&dropdown, menu,
                                     event->button.x, event->button.y);
      if ((item_index >= 0) && menu->items[item_index].enabled && command) {
        *command = menu->items[item_index].command;
      }
      state->active_menu = -1;
      state->hot_item = -1;
      return true;
    }
    return false;
  }

  if ((event->type == SDL_MOUSEMOTION) && (state->active_menu >= 0)) {
    if (event->motion.y < MENU_BAR_HEIGHT) {
      int menu_index = menu_at_x(state->font, menus, menu_count,
                                 event->motion.x);
      if ((menu_index >= 0) && (menu_index != state->active_menu)) {
        open_menu(state, menus, menu_index);
      }
    } else {
      const menu_bar_menu_t* menu = &menus[state->active_menu];
      SDL_Rect dropdown = dropdown_rect(renderer, state->font, menus,
                                        state->active_menu);
      int item_index = item_at_point(&dropdown, menu,
                                     event->motion.x, event->motion.y);
      state->hot_item = (item_index >= 0 && menu->items[item_index].enabled)
        ? item_index
        : -1;
    }
    return true;
  }

  if ((event->type != SDL_KEYDOWN) || (state->active_menu < 0)) {
    return false;
  }

  SDL_KeyCode key = event->key.keysym.sym;
  if (key == SDLK_ESCAPE) {
    state->active_menu = -1;
    state->hot_item = -1;
  } else if ((key == SDLK_LEFT) || (key == SDLK_RIGHT)) {
    int direction = key == SDLK_LEFT ? -1 : 1;
    int next_menu = (state->active_menu + direction + menu_count) % menu_count;
    open_menu(state, menus, next_menu);
  } else if ((key == SDLK_UP) || (key == SDLK_DOWN)) {
    int direction = key == SDLK_UP ? -1 : 1;
    state->hot_item = next_enabled_item(&menus[state->active_menu],
                                        state->hot_item, direction);
  } else if ((key == SDLK_RETURN) || (key == SDLK_KP_ENTER)) {
    const menu_bar_menu_t* menu = &menus[state->active_menu];
    if ((state->hot_item >= 0) && menu->items[state->hot_item].enabled &&
        command) {
      *command = menu->items[state->hot_item].command;
    }
    state->active_menu = -1;
    state->hot_item = -1;
  }
  return true;
}

void menu_bar_render(SDL_Renderer* renderer,
                     const menu_bar_state_t* state,
                     const menu_bar_menu_t* menus, int menu_count) {
  if (!renderer || !state || !state->font || !menus) {
    return;
  }

  int renderer_width = 0;
  SDL_GetRendererOutputSize(renderer, &renderer_width, NULL);
  SDL_SetRenderDrawColor(renderer, MENU_BAR_BACKGROUND.r,
                         MENU_BAR_BACKGROUND.g, MENU_BAR_BACKGROUND.b, 255);
  SDL_Rect bar = {0, 0, renderer_width, MENU_BAR_HEIGHT};
  SDL_RenderFillRect(renderer, &bar);
  SDL_SetRenderDrawColor(renderer, MENU_BORDER.r, MENU_BORDER.g,
                         MENU_BORDER.b, 255);
  SDL_RenderDrawLine(renderer, 0, MENU_BAR_HEIGHT - 1,
                     renderer_width, MENU_BAR_HEIGHT - 1);

  int x = 0;
  int text_y = (MENU_BAR_HEIGHT - TTF_FontHeight(state->font)) / 2;
  for (int index = 0; index < menu_count; index++) {
    int width = top_menu_width(state->font, &menus[index]);
    if (index == state->active_menu) {
      SDL_SetRenderDrawColor(renderer, MENU_HIGHLIGHT.r,
                             MENU_HIGHLIGHT.g, MENU_HIGHLIGHT.b, 255);
      SDL_Rect highlight = {x, 0, width, MENU_BAR_HEIGHT - 1};
      SDL_RenderFillRect(renderer, &highlight);
    }
    draw_text(renderer, state->font, menus[index].label,
              index == state->active_menu ? MENU_HIGHLIGHT_TEXT : MENU_TEXT,
              x + MENU_TOP_PADDING, text_y);
    x += width;
  }

  if (state->active_menu < 0 || state->active_menu >= menu_count) {
    return;
  }

  const menu_bar_menu_t* menu = &menus[state->active_menu];
  SDL_Rect dropdown = dropdown_rect(renderer, state->font, menus,
                                    state->active_menu);
  SDL_SetRenderDrawColor(renderer, MENU_DROP_BACKGROUND.r,
                         MENU_DROP_BACKGROUND.g, MENU_DROP_BACKGROUND.b, 255);
  SDL_RenderFillRect(renderer, &dropdown);
  SDL_SetRenderDrawColor(renderer, MENU_BORDER.r, MENU_BORDER.g,
                         MENU_BORDER.b, 255);
  SDL_RenderDrawRect(renderer, &dropdown);

  for (int index = 0; index < menu->item_count; index++) {
    const menu_bar_item_t* item = &menu->items[index];
    SDL_Rect row = item_rect(&dropdown, menu, index);
    if (item_is_separator(item)) {
      int separator_y = row.y + row.h / 2;
      SDL_RenderDrawLine(renderer, row.x + MENU_DROP_PADDING, separator_y,
                         row.x + row.w - MENU_DROP_PADDING, separator_y);
      continue;
    }

    bool highlighted = index == state->hot_item && item->enabled;
    if (highlighted) {
      SDL_SetRenderDrawColor(renderer, MENU_HIGHLIGHT.r,
                             MENU_HIGHLIGHT.g, MENU_HIGHLIGHT.b, 255);
      SDL_RenderFillRect(renderer, &row);
    }
    SDL_Color colour = highlighted
      ? MENU_HIGHLIGHT_TEXT
      : (item->enabled ? MENU_TEXT : MENU_DISABLED);
    int row_text_y = row.y + (row.h - TTF_FontHeight(state->font)) / 2;
    if (item->checked) {
      draw_text(renderer, state->font, "x", colour,
                row.x + MENU_DROP_PADDING, row_text_y);
    }
    draw_text(renderer, state->font, item->label, colour,
              row.x + MENU_DROP_PADDING + MENU_CHECK_WIDTH, row_text_y);
    if (item->shortcut && *item->shortcut) {
      int shortcut_width = text_width(state->font, item->shortcut);
      draw_text(renderer, state->font, item->shortcut, colour,
                row.x + row.w - MENU_DROP_PADDING - shortcut_width,
                row_text_y);
    }
  }
}
