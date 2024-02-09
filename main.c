#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <SDL.h>
#include <time.h>
#include <unistd.h>

#include "cpu_6502.h"
#include "display.h"
#include "eprom.h"
#include "function_return_codes.h"
#include "joystick.h"
#include "keyboard.h"
#include "popup.h"
#include "system.h"
#include "via_6522.h"

#define MICROTAN_DEFAULT_CLOCK_FREQUENCY        750000
#define LOOP_EXECUTE_TIME_MS                    20

const char *SETTINGS_FILE = "microtan_settings.txt";

void save_window_settings(SDL_Window *window)
{
    int x, y, width, height;
    SDL_GetWindowSize(window, &width, &height);
    SDL_GetWindowPosition(window, &x, &y);
    FILE *file = fopen(SETTINGS_FILE, "w");

    if (file)
    {
        fprintf(file, "%d %d %d %d", x, y, width, height);
        fclose(file);
    }
}

void load_window_settings(int *x, int *y, int *width, int *height)
{
    FILE *file = fopen(SETTINGS_FILE, "r");

    if (file)
    {
        fscanf(file, "%d %d %d %d", x, y, width, height);
        fclose(file);
    }
}

void set_pixel(SDL_Surface *surface, int x, int y, Uint32 pixel)
{
    Uint8 *target_pixel = (Uint8 *)surface->pixels + y * surface->pitch + x * 4;
    *(Uint32 *)target_pixel = pixel;
}

SDL_Texture *create_scanline_texture(SDL_Renderer *renderer, int width, int height)
{
    int scan_width = 1;
    int scan_height = 4;
    int texture_width = width * scan_width;
    int texture_height = height * scan_height;
    SDL_Surface *scanline_surface = SDL_CreateRGBSurface(0, texture_width, texture_height, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);

    if (!scanline_surface)
    {
        return NULL;
    }

    uint32_t transparent_100 = SDL_MapRGBA(scanline_surface->format, 0, 0, 0, 0);
    uint32_t transparent_75 = SDL_MapRGBA(scanline_surface->format, 0, 0, 0, 64);
    uint32_t transparent_50 = SDL_MapRGBA(scanline_surface->format, 0, 0, 0, 128);
    uint32_t transparent_25 = SDL_MapRGBA(scanline_surface->format, 0, 0, 0, 192);
    uint32_t red = SDL_MapRGBA(scanline_surface->format, 255, 0, 0, 255);
    uint32_t white = SDL_MapRGBA(scanline_surface->format, 255, 255, 255, 255);

    for (int y = 0; y < texture_height; y += scan_height)
    {
        for (int x = 0; x < texture_width; x++)
        {
            set_pixel(scanline_surface, x, y, transparent_100);
            set_pixel(scanline_surface, x, y + 1, transparent_100);
            set_pixel(scanline_surface, x, y + 2, transparent_50);
            set_pixel(scanline_surface, x, y + 3, transparent_25);
        }
    }

    SDL_Texture *scanline_texture = SDL_CreateTextureFromSurface(renderer, scanline_surface);
    SDL_FreeSurface(scanline_surface);
    return scanline_texture;
}

int main(int argc, char *argv[])
{
    if (system_initialise() != RV_OK)
    {
        return 0;
    }

    system_reset();

    if (argc > 1)
    {
        system_load_m65_file(argv[1]);

        if (strstr(argv[1], "berzerk.m65") != NULL)
        {
            keyboard_use_hex_keypad(true);
        }
    }
    srand(time(NULL));

    SDL_Init(SDL_INIT_VIDEO);
    int x = SDL_WINDOWPOS_CENTERED, y = SDL_WINDOWPOS_CENTERED, width = DISPLAY_WIDTH, height = DISPLAY_HEIGHT;
    load_window_settings(&x, &y, &width, &height);
    SDL_Window *window = SDL_CreateWindow("Microtan 65", x, y, width, height, SDL_WINDOW_RESIZABLE);
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    SDL_Texture *scanlines = create_scanline_texture(renderer, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    uint32_t pixels[DISPLAY_WIDTH * DISPLAY_HEIGHT];
    bool is_running = true;
    SDL_Event event;
    uint32_t cpu_clock_frequency = MICROTAN_DEFAULT_CLOCK_FREQUENCY;
    struct timespec start_time;
    struct timespec end_time;
    struct timespec sleep_time;
    bool display_overwritten = false;

    while (is_running)
    {
        clock_gettime(CLOCK_MONOTONIC, &start_time);
        // Execute LOOP_EXECUTE_TIME_MS's worth of instructions
        cpu_6502_execute(MICROTAN_DEFAULT_CLOCK_FREQUENCY * LOOP_EXECUTE_TIME_MS / 1000);

        // If the display has been updated, re-render the window
        if ((display_updated_event()) || (display_overwritten))
        {
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

        while (SDL_PollEvent(&event))
        {
            switch (event.type)
            {
            case SDL_QUIT:
                is_running = false;
                break;

            case SDL_WINDOWEVENT:
                switch (event.window.event)
                {
                case SDL_WINDOWEVENT_RESIZED:
                    {
                        int new_width = event.window.data1;
                        int new_height = event.window.data2 & 0xffffff00;

                        if (new_height < 512)
                        {
                            new_height = 512;
                        }

                        // Calculate desired height for 5:4 aspect ratio
                        int desired_width = (5 * new_height) / 4;

                        if (new_width != desired_width)
                        {
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

            case SDL_TEXTINPUT:
                {
                    uint8_t *ascii_value = event.text.text;

                    while (*ascii_value)
                    {
                        // Microtan requires capitals for all commands, so swap upper/lower case for convenience
                        uint8_t key = *ascii_value;

                        if (((key >= 'A') && (key <= 'Z')) || ((key >= 'a') && (key <= 'z')))
                        {
                            key ^= 0x20;
                        }

                        keyboard_keypress(key);
                        ascii_value++;
                    }
                }
                break;

            case SDL_KEYDOWN:
                {
                    SDL_KeyCode keycode = event.key.keysym.sym;

                    if (keycode == SDLK_F1)
                    {
                        popup_show(renderer,
                                   "F1: Display this help\n" \
                                   "F2: Select hex keypad input\n" \
                                   "F3: Select ASCII keyboard input\n" \
                                   "F5: Reset system\n" \
                                   "\n"
                                  );
                        display_overwritten = true;
                    }
                    else if (keycode == SDLK_F2)
                    {
                        keyboard_use_hex_keypad(true);
                    }
                    else if (keycode == SDLK_F3)
                    {
                        keyboard_use_hex_keypad(false);
                    }
                    else if (keycode == SDLK_F5)
                    {
                        system_reset();
                    }
                    else
                    {
                        if (keycode == SDLK_KP_ENTER)
                        {
                            keycode = 0x0a;
                        }
                        else if ((SDL_GetModState() & KMOD_CTRL) && (keycode >= 'a') && (keycode <= 'z'))
                        {
                            keycode -= 0x60;
                        }
                        else if ((SDL_GetModState() & KMOD_CTRL) && (keycode >= 'A') && (keycode <= 'Z'))
                        {
                            keycode -= 0x40;
                        }
                        else if (keycode == 0x08)
                        {
                            keycode = 0x7f;
                        }

                        if ((keycode < ' ') || (keycode == 0x7f))
                        {
                            keyboard_keypress(keycode);
                        }
                    }
                }
                break;
            } // SDL event switch
        } // SDL event loop

        // via_6522_print_regs();
        joystick();
        clock_gettime(CLOCK_MONOTONIC, &end_time);
        long elapsed_time = (end_time.tv_sec - start_time.tv_sec) * 1000 + (end_time.tv_nsec - start_time.tv_nsec) / 1000000;

        if (elapsed_time < LOOP_EXECUTE_TIME_MS)
        {
            sleep_time.tv_sec = 0;
            sleep_time.tv_nsec = (LOOP_EXECUTE_TIME_MS - elapsed_time) * 1000000;
            nanosleep(&sleep_time, NULL);
        }
    } // main loop

    save_window_settings(window);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    system_close();

    return 0;
}
