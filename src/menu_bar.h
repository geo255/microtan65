#ifndef __MENU_BAR_H__
#define __MENU_BAR_H__

#include <SDL.h>
#include <SDL_ttf.h>
#include <stdbool.h>

#define MENU_BAR_HEIGHT 30
#define MENU_BAR_SEPARATOR_COMMAND (-1)

typedef struct {
  const char* label;
  const char* shortcut;
  int command;
  bool enabled;
  bool checked;
} menu_bar_item_t;

typedef struct {
  const char* label;
  const menu_bar_item_t* items;
  int item_count;
} menu_bar_menu_t;

typedef struct {
  TTF_Font* font;
  int active_menu;
  int hot_item;
} menu_bar_state_t;

extern bool menu_bar_initialise(menu_bar_state_t* state);
extern void menu_bar_close(menu_bar_state_t* state);
extern bool menu_bar_handle_event(menu_bar_state_t* state,
                                  SDL_Renderer* renderer,
                                  const SDL_Event* event,
                                  const menu_bar_menu_t* menus,
                                  int menu_count,
                                  int* command);
extern void menu_bar_render(SDL_Renderer* renderer,
                            const menu_bar_state_t* state,
                            const menu_bar_menu_t* menus,
                            int menu_count);

#endif // __MENU_BAR_H__
