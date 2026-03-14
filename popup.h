#ifndef __POPUP_H__
#define __POPUP_H__

#include <SDL.h>
#include <stdbool.h>
#include <stddef.h>

extern void popup_show(SDL_Renderer* renderer, const char* message);
extern int popup_menu_select(SDL_Renderer* renderer, const char* title, const char* const* items, int item_count, int selected_index);
extern bool popup_prompt_input(SDL_Renderer* renderer, const char* title, const char* prompt, const char* default_value, char* output, size_t output_size);
extern bool popup_file_select(SDL_Renderer* renderer,
                              const char* title,
                              const char* start_directory,
                              const char* const* extensions,
                              int extension_count,
                              bool allow_text_entry,
                              const char* default_name,
                              char* output,
                              size_t output_size);

#endif // __POPUP_H__
