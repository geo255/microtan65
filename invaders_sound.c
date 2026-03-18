#include "external_filenames.h"
#include "invaders_sound.h"
#include "system.h"
#include <SDL.h>
#include <SDL_mixer.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* ---------------------------------------------------------------------------
 * Space Invaders Sound Effects Handler
 *
 * Memory-mapped register at 0xBC04 controls game sound effects:
 *   Bit 7 (0x80): 1=Disable all sounds, 0=Enable
 *   Bit 5 (0x20): Hit sound
 *   Bit 4 (0x10): Explosion
 *   Bit 3 (0x08): Saucer (looping)
 *   Bit 2 (0x04): Laser
 *   Bit 1 (0x02): Heartbeat (alternating between two samples)
 * --------------------------------------------------------------------------*/

#define MAX_SOUNDS 6

typedef enum {
  SND_SAUCER = 0,
  SND_SAUCEREND,
  SND_HEARTBEAT1,
  SND_HEARTBEAT2,
  SND_LASER,
  SND_HIT,
  SND_EXPLOSION
} sound_id_t;

static Mix_Chunk* sounds[MAX_SOUNDS + 1];
static int saucer_channel = -1; /* Channel for looping saucer sound */
static bool invaders_sound_initialized = false;

static uint8_t heartbeat = 0;
static uint8_t prev_value = 0xFF;
static bool do_explosion = true;

/* Sound file paths */
static const char* sound_files[] = {
  ASSETS_SOUNDS_DIRECTORY "/saucer.wav",     /* SND_SAUCER */
  ASSETS_SOUNDS_DIRECTORY "/saucerend.wav",  /* SND_SAUCEREND */
  ASSETS_SOUNDS_DIRECTORY "/heartbeat1.wav", /* SND_HEARTBEAT1 */
  ASSETS_SOUNDS_DIRECTORY "/heartbeat2.wav", /* SND_HEARTBEAT2 */
  ASSETS_SOUNDS_DIRECTORY "/laser.wav",      /* SND_LASER */
  ASSETS_SOUNDS_DIRECTORY "/hit.wav",        /* SND_HIT */
  ASSETS_SOUNDS_DIRECTORY "/explosion.wav"   /* SND_EXPLOSION */
};

/* ---------------------------------------------------------------------------
 * Cleanup - free all sound chunks
 * --------------------------------------------------------------------------*/
void invaders_sound_close(void) {
  if (!invaders_sound_initialized)
    return;

  /* Stop all sounds */
  Mix_HaltChannel(-1);

  /* Free sound chunks */
  for (int i = 0; i <= MAX_SOUNDS; i++) {
    if (sounds[i]) {
      Mix_FreeChunk(sounds[i]);
      sounds[i] = NULL;
    }
  }

  invaders_sound_initialized = false;
}

/* ---------------------------------------------------------------------------
 * Play a sound effect
 *
 * loop: -1 = infinite loop, 0 = play once, >0 = loop N times
 * Returns channel number or -1 on error
 * --------------------------------------------------------------------------*/
static int play_sound(sound_id_t sound, int loops) {
  if (!invaders_sound_initialized || !sounds[sound])
    return -1;

  return Mix_PlayChannel(-1, sounds[sound], loops);
}

/* ---------------------------------------------------------------------------
 * Memory-mapped write handler for 0xBC04
 * --------------------------------------------------------------------------*/
void invaders_sound_write_callback(uint16_t address, uint8_t value) {
  (void)address;

  if (!invaders_sound_initialized) {
    prev_value = value;
    return;
  }

  /* Bit 7: Disable all sounds */
  if (value & 0x80) {
    /* Stop everything and purge */
    Mix_HaltChannel(-1);
    saucer_channel = -1;
    prev_value = value;
    return;
  }

  /* Bit 3: Saucer sound (looping) */
  if ((value & 0x08) != (prev_value & 0x08)) {
    if (value & 0x08) {
      /* Start saucer loop */
      saucer_channel = play_sound(SND_SAUCER, -1); /* Loop forever */
    } else {
      /* Stop saucer, play end sound */
      if (saucer_channel >= 0) {
        Mix_HaltChannel(saucer_channel);
        saucer_channel = -1;
      }
      play_sound(SND_SAUCEREND, 0);
    }
  }

  /* Only play other sounds when saucer is not active */
  if (!(value & 0x08)) {
    /* Bit 1: Heartbeat (alternates between two samples) */
    if (((value & 0x02) != (prev_value & 0x02)) && (value & 0x02)) {
      if (heartbeat == 0)
        play_sound(SND_HEARTBEAT1, 0);
      else
        play_sound(SND_HEARTBEAT2, 0);

      heartbeat = 1 - heartbeat;
      do_explosion = true;
    }

    /* Bit 2: Laser */
    if (((value & 0x04) != (prev_value & 0x04)) && (value & 0x04)) {
      play_sound(SND_LASER, 0);
      do_explosion = true;
    }

    /* Bit 5: Hit */
    if (((value & 0x20) != (prev_value & 0x20)) && (value & 0x20)) {
      play_sound(SND_HIT, 0);
      do_explosion = true;
    }

    /* Bit 4: Explosion (only play once per sequence) */
    if (((value & 0x10) != (prev_value & 0x10)) && (value & 0x10) && do_explosion) {
      play_sound(SND_EXPLOSION, 0);
      do_explosion = false;
    }
  }

  prev_value = value;
}

/* ---------------------------------------------------------------------------
 * Reset sound state (call when game resets)
 * --------------------------------------------------------------------------*/
void invaders_sound_reset(uint8_t bank, uint16_t address) {
  (void)bank;
  (void)address;
  if (!invaders_sound_initialized)
    return;

  Mix_HaltChannel(-1);
  saucer_channel = -1;
  heartbeat = 0;
  prev_value = 0xFF;
  do_explosion = true;
}

/* ---------------------------------------------------------------------------
 * Initialize SDL_mixer and load WAV files
 * --------------------------------------------------------------------------*/
int invaders_sound_initialise(uint8_t bank, uint16_t address, uint16_t param, char* identifier) {
  (void)bank;
  (void)address;
  (void)param;
  (void)identifier;
  if (invaders_sound_initialized)
    return 0;

  /* SDL_mixer should already be initialized by main, but check */
  if (Mix_OpenAudio(22050, MIX_DEFAULT_FORMAT, 2, 2048) != 0) {
    printf("Warning: Mix_OpenAudio failed: %s\n", Mix_GetError());
    printf("         Space Invaders sound effects disabled\n");
    return -1;
  }

  /* Allocate 8 mixing channels (one for saucer loop, rest for one-shots) */
  Mix_AllocateChannels(8);

  /* Load all sound effects */
  for (int i = 0; i <= MAX_SOUNDS; i++) {
    sounds[i] = Mix_LoadWAV(sound_files[i]);
    if (!sounds[i]) {
      printf("Warning: Failed to load %s: %s\n",
             sound_files[i],
             Mix_GetError());
      /* Continue anyway - missing sounds just won't play */
    }
  }
  system_register_memory_mapped_device(address, address, NULL, invaders_sound_write_callback, true);

  invaders_sound_initialized = true;
  printf("Space Invaders sound effects initialized\n");
  return 0;
}
