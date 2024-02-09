#include <dirent.h>
#include <stdbool.h>
#include <stdint.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

void popup_show(SDL_Renderer *renderer, const char *message)
{
    SDL_Color text_color = {0, 0, 0, 255};
    SDL_Color bg_color = {242, 133, 0, 128};
    SDL_Rect popup_rect = {100, 100, 400, 300};
    char font_path[256];
    DIR *d;
    struct dirent *dir;
    d = opendir(".");
    font_path[0] = 0;

    if (d)
    {
        while ((dir = readdir(d)) != NULL)
        {
            char *dot = strrchr(dir->d_name, '.');

            if (dot && strcmp(dot, ".ttf") == 0)
            {
                strcpy(font_path, dir->d_name);
                break;
            }
        }

        closedir(d);
    }

    if (font_path[0] == 0)
    {
        printf("Font file missing\r\n");
        return;
    }

    if (TTF_Init() == -1)
    {
        printf("TTF_Init: %s\n", TTF_GetError());
        return;
    }

    TTF_Font *font = TTF_OpenFont(font_path, 24);

    if (font == NULL)
    {
        printf("TTF_OpenFont: %s\n", TTF_GetError());
        TTF_Quit();
        return;
    }

    char *message_copy = strdup(message);
    char *lines[64];
    int num_lines = 0;
    char *line = strtok(message_copy, "\r\n");

    while (line && num_lines < 64)
    {
        lines[num_lines++] = line;
        line = strtok(NULL, "\r\n");
    }

    SDL_Texture *text_textures[10];
    int max_text_width = 0;

    for (int i = 0; i < num_lines; i++)
    {
        SDL_Surface *text_surface = TTF_RenderText_Blended(font, lines[i], text_color);
        text_textures[i] = SDL_CreateTextureFromSurface(renderer, text_surface);

        if (text_surface->w > max_text_width)
        {
            max_text_width = text_surface->w;
        }

        SDL_FreeSurface(text_surface);
    }

    if (popup_rect.w < max_text_width + 80)
    {
        popup_rect.w = max_text_width + 80;
    }

    bool popup_done = false;

    while (!popup_done)
    {
        SDL_Event event;

        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT)
            {
                popup_done = true;
            }
            else if (event.type == SDL_KEYDOWN)
            {
                popup_done = true;
            }
            else if (event.type == SDL_MOUSEBUTTONDOWN)
            {
                int x, y;
                SDL_GetMouseState(&x, &y);

                if (x >= popup_rect.x && x < popup_rect.x + popup_rect.w &&
                        y >= popup_rect.y && y < popup_rect.y + popup_rect.h)
                {
                    popup_done = true;
                }
            }
        }

        SDL_SetRenderDrawColor(renderer, bg_color.r, bg_color.g, bg_color.b, bg_color.a);
        SDL_RenderFillRect(renderer, &popup_rect);
        int border_thickness = 6;
        SDL_Rect outer_border_rect = {popup_rect.x + 2 * border_thickness, popup_rect.y + 2 * border_thickness, popup_rect.w - 4 * border_thickness, popup_rect.h - 4 * border_thickness};
        SDL_Rect inner_border_rect = {outer_border_rect.x + 2 * border_thickness, outer_border_rect.y + 2 * border_thickness, outer_border_rect.w - 4 * border_thickness, outer_border_rect.h - 4 * border_thickness};
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);

        for (int i = 0; i < border_thickness; i++)
        {
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

        int y = inner_border_rect.y + 10;

        for (int i = 0; i < num_lines; i++)
        {
            int text_w;
            int text_h;
            SDL_QueryTexture(text_textures[i], NULL, NULL, &text_w, &text_h);
            SDL_Rect text_rect = { popup_rect.x + (popup_rect.w - text_w) / 2, y, text_w, text_h };
            SDL_RenderCopy(renderer, text_textures[i], NULL, &text_rect);
            y += text_h + 5;
        }

        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    for (int i = 0; i < num_lines; i++)
    {
        SDL_DestroyTexture(text_textures[i]);
    }
}

