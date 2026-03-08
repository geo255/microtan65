#include "ay8910.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef S_ISSOCK
#ifdef S_IFSOCK
#define S_ISSOCK(mode) (((mode) & S_IFMT) == S_IFSOCK)
#else
#define S_ISSOCK(mode) (0)
#endif
#endif

/* ---------------------------------------------------------------------------
 * AY-3-8910 Programmable Sound Generator Emulation
 * --------------------------------------------------------------------------*/

/* ---------------------------------------------------------------------------
 * Configuration
 * --------------------------------------------------------------------------*/
#define PLAYBACK_FREQUENCY 22050 /* Hz - mono, 8-bit unsigned */
#define MAX_DEVICES        2     /* Microtan 65 has two AY8910s */

/* SDL audio callback buffer size.  22050 Hz / 20 updates/sec = 1102 samples.
 * Round up to a power-of-two friendly size. */
#define AUDIO_BUF_SIZE 2048

/* ---------------------------------------------------------------------------
 * Module state
 * --------------------------------------------------------------------------*/
static ay8910_t chips[MAX_DEVICES];
static bool ay8910_initialised = false;
static SDL_AudioDeviceID audio_device = 0;
static uint16_t address_table[MAX_DEVICES] = {0xbc00, 0xbc02};
static uint8_t ay8910_memory_mapped_registers[MAX_DEVICES][2];

/* Each chip has its own output buffer; the SDL callback mixes them together. */
static uint8_t chip_buffer[MAX_DEVICES][AUDIO_BUF_SIZE];

/* ---------------------------------------------------------------------------
 * Envelope waveform table  (16 shapes x 32 steps)
 * --------------------------------------------------------------------------*/
static const uint8_t envelope_forms[16][32] =
  {
    {15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0},
    {15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
    {15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0},
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}};

/* ---------------------------------------------------------------------------
 * Per-chip sample generation
 * --------------------------------------------------------------------------*/
static void update_chip(int num, int num_samples) {
  ay8910_t* psg = &chips[num];
  int x;
  int c0, c1, l0, l1, l2;
  uint8_t* lpb;

  x = psg->regs[AY_AFINE] + ((unsigned)(psg->regs[AY_ACOARSE] & 0x0F) << 8);
  psg->inc_0 = x ? (int)((long)AY8910_CLOCK / PLAYBACK_FREQUENCY * 4 / x) : 0;

  x = psg->regs[AY_BFINE] + ((unsigned)(psg->regs[AY_BCOARSE] & 0x0F) << 8);
  psg->inc_1 = x ? (int)((long)AY8910_CLOCK / PLAYBACK_FREQUENCY * 4 / x) : 0;

  x = psg->regs[AY_CFINE] + ((unsigned)(psg->regs[AY_CCOARSE] & 0x0F) << 8);
  psg->inc_2 = x ? (int)((long)AY8910_CLOCK / PLAYBACK_FREQUENCY * 4 / x) : 0;

  x = psg->regs[AY_NOISEPER] & 0x1F;
  psg->inc_noise = (int)((long)AY8910_CLOCK / PLAYBACK_FREQUENCY * 4 / (x ? x : 1));

  x = psg->regs[AY_EFINE] + ((unsigned)psg->regs[AY_ECOARSE] << 8);
  psg->inc_env = x ? (int)((long)AY8910_CLOCK / PLAYBACK_FREQUENCY * 4 / x * num_samples) : 0;

  /* Sample the envelope table once per update call */
  psg->envelope = envelope_forms[psg->regs[AY_ESHAPE]][(psg->count_env >> 16) & 0x1F];

  if ((psg->count_env += psg->inc_env) & 0xFFE00000) {
    switch (psg->regs[AY_ESHAPE]) {
      case 8:
      case 10:
      case 12:
      case 14:
        psg->count_env -= 0x200000;
        break;
      default:
        psg->count_env = 0x100000;
        psg->inc_env = 0;
        break;
    }
  }

  /* Resolve volumes: bit 4 of VOLx selects envelope vs. fixed */
  psg->volume_0 = (psg->regs[AY_AVOL] < 16) ? psg->regs[AY_AVOL] : psg->envelope;
  psg->volume_1 = (psg->regs[AY_BVOL] < 16) ? psg->regs[AY_BVOL] : psg->envelope;
  psg->volume_2 = (psg->regs[AY_CVOL] < 16) ? psg->regs[AY_CVOL] : psg->envelope;

  /* Noise volume = average of the channels that have noise enabled */
  psg->volume_noise = (((psg->regs[AY_ENABLE] & 010) ? 0 : psg->volume_0) +
                       ((psg->regs[AY_ENABLE] & 020) ? 0 : psg->volume_1) +
                       ((psg->regs[AY_ENABLE] & 040) ? 0 : psg->volume_2)) /
                      2;

  /* Mask out tone channels that are disabled */
  psg->volume_0 = (psg->regs[AY_ENABLE] & 001) ? 0 : psg->volume_0;
  psg->volume_1 = (psg->regs[AY_ENABLE] & 002) ? 0 : psg->volume_1;
  psg->volume_2 = (psg->regs[AY_ENABLE] & 004) ? 0 : psg->volume_2;

  lpb = psg->buffer;

  for (int i = 0; i < num_samples; i++) {
    /* --- Channel A anti-aliased square wave --- */
    c0 = psg->counter_0;
    c1 = psg->counter_0 + psg->inc_0;
    l0 = ((c0 & 0x8000) ? -16 : 16);

    if ((c0 ^ c1) & 0x8000) {
      l0 = l0 * (0x8000 - (c0 & 0x7FFF) - (c1 & 0x7FFF)) / psg->inc_0;
    }

    psg->counter_0 = c1 & 0xFFFF;

    /* --- Channel B --- */
    c0 = psg->counter_1;
    c1 = psg->counter_1 + psg->inc_1;
    l1 = ((c0 & 0x8000) ? -16 : 16);

    if ((c0 ^ c1) & 0x8000) {
      l1 = l1 * (0x8000 - (c0 & 0x7FFF) - (c1 & 0x7FFF)) / psg->inc_1;
    }

    psg->counter_1 = c1 & 0xFFFF;

    /* --- Channel C --- */
    c0 = psg->counter_2;
    c1 = psg->counter_2 + psg->inc_2;
    l2 = ((c0 & 0x8000) ? -16 : 16);

    if ((c0 ^ c1) & 0x8000) {
      l2 = l2 * (0x8000 - (c0 & 0x7FFF) - (c1 & 0x7FFF)) / psg->inc_2;
    }

    psg->counter_2 = c1 & 0xFFFF;

    /* --- Noise LFSR --- */
    psg->count_noise &= 0xFFFF;

    if ((psg->count_noise += psg->inc_noise) & 0xFFFF0000) {
      psg->state_noise =
        ((psg->noise_gen <<= 1)& 0x80000000
           ? psg->noise_gen ^= 0x00040001
           : psg->noise_gen) &
        1;
    }

    /* --- Mix and write sample --- */
    *lpb++ = (uint8_t)AUDIO_CONV(
      (l0 * psg->volume_0 + l1 * psg->volume_1 + l2 * psg->volume_2) / 6 +
      (psg->state_noise ? psg->volume_noise : -psg->volume_noise));
  }
}

/* ---------------------------------------------------------------------------
 * SDL audio callback  - called from SDL's audio thread.
 * Generates fresh samples for both chips, then averages them into the
 * output buffer.
 * --------------------------------------------------------------------------*/
static void audio_callback(void* userdata, uint8_t* stream, int len) {
  (void)userdata;

  if (!ay8910_initialised) {
    memset(stream, 128, len); /* silence */
    return;
  }

  /* len is in bytes; for 8-bit mono that equals number of samples.
   * Clamp to our internal buffer size just in case SDL asks for more. */
  int num_samples = (len < AUDIO_BUF_SIZE) ? len : AUDIO_BUF_SIZE;

  /* Generate samples for each chip */
  update_chip(0, num_samples);
  update_chip(1, num_samples);

  /* Mix: average the two chips into the output stream */
  for (int i = 0; i < num_samples; i++) {
    stream[i] = (uint8_t)(((unsigned)chip_buffer[0][i] + (unsigned)chip_buffer[1][i]) / 2);
  }
}

/* ---------------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------------*/

void ay8910_write_callback(uint16_t address, uint8_t value) {
  int n = -1;
  int r = 0;

  if (!ay8910_initialised) {
    return;
  }

  for (int i = 0; (i < MAX_DEVICES) && (n < 0); i++) {
    if (address == address_table[i]) {
      ay8910_memory_mapped_registers[i][0] = value;
      return;
    } else if (address == (address_table[i] + 1)) {
      ay8910_memory_mapped_registers[i][1] = value;
      n = i;
      r = ay8910_memory_mapped_registers[i][0] & 0x0f;
    }
  }

  if (n < 0 || n >= MAX_DEVICES)
    return;

  /* Lock audio to prevent conflicts with audio callback */
  if (audio_device != 0)
    SDL_LockAudioDevice(audio_device);

  ay8910_t* psg = &chips[n];
  psg->regs[r] = value;

  switch (r) {
    case AY_AVOL:
    case AY_BVOL:
    case AY_CVOL:
      psg->regs[r] &= 0x1F; /* volume is 5 bits */
      break;

    case AY_ESHAPE:
      psg->count_env = 0; /* writing shape resets envelope counter */
      psg->regs[AY_ESHAPE] &= 0x0F;
      break;

    case AY_PORTA:
      if (psg->port[0])
        psg->port[0](psg, AY_PORTA, 1, value);
      break;

    case AY_PORTB:
      if (psg->port[1])
        psg->port[1](psg, AY_PORTB, 1, value);
      break;
  }

  if (audio_device != 0)
    SDL_UnlockAudioDevice(audio_device);
}

uint8_t ay8910_read_callback(uint16_t address) {
  int n = -1;
  int r = 0;

  if (!ay8910_initialised) {
    return 0xff;
  }

  for (int i = 0; (i < MAX_DEVICES) && (n < 0); i++) {
    if (address == address_table[i]) {
      return ay8910_memory_mapped_registers[i][0];
    } else if (address == (address_table[i] + 1)) {
      n = i;
      r = ay8910_memory_mapped_registers[i][0] & 0x0f;
    }
  }

  if (n < 0 || n >= MAX_DEVICES)
    return 0xff;

  /* Lock audio to prevent conflicts with audio callback */
  if (audio_device != 0)
    SDL_LockAudioDevice(audio_device);

  ay8910_t* psg = &chips[n];

  switch (r) {
    case AY_PORTA:
      if (psg->port[0])
        psg->port[0](psg, AY_PORTA, 0, 0);
      break;

    case AY_PORTB:
      if (psg->port[1])
        psg->port[1](psg, AY_PORTB, 0, 0);
      break;
  }

  uint8_t result = psg->regs[r];

  if (audio_device != 0)
    SDL_UnlockAudioDevice(audio_device);

  return result;
}

void ay8910_set_port_handler(int n, int port, ay8910_port_handler_t func) {
  int idx = port - AY_PORTA;

  if (n < 0 || n >= MAX_DEVICES || idx < 0 || idx > 1)
    return;

  chips[n].port[idx] = func;
}

static void reset_chip(int num) {
  ay8910_t* psg = &chips[num];

  memset(psg->buffer, 0, AUDIO_BUF_SIZE);
  memset(psg->regs, 0, sizeof(psg->regs));

  psg->noise_gen = 1;
  psg->envelope = 15;
  psg->state_noise = 0;

  psg->inc_0 = psg->inc_1 = psg->inc_2 = 0;
  psg->inc_env = psg->inc_noise = 0;
  psg->counter_0 = psg->counter_1 = psg->counter_2 = 0;
  psg->count_env = psg->count_noise = 0;
  psg->volume_0 = psg->volume_1 = psg->volume_2 = 0;
  psg->volume_noise = 0;
}

int ay8910_initialise(uint8_t bank, uint16_t address, uint16_t param, char* identifier) {
  (void)bank;
  (void)address;
  (void)param;
  (void)identifier;

  if (ay8910_initialised)
    return 0; /* already open */

  /* Point each chip's buffer at its slot in chip_buffer */
  for (int i = 0; i < MAX_DEVICES; i++) {
    chips[i].buffer = chip_buffer[i];
    chips[i].port[0] = chips[i].port[1] = NULL;
    reset_chip(i);
  }

  /* For WSLg support - point PulseAudio to WSLg server if it exists */
  const char* wslg_pulse = "/mnt/wslg/PulseServer";
  struct stat st;
  if (stat(wslg_pulse, &st) == 0 && S_ISSOCK(st.st_mode)) {
    char pulse_server[256];
    snprintf(pulse_server, sizeof(pulse_server), "unix:%s", wslg_pulse);
    setenv("PULSE_SERVER", pulse_server, 0); /* don't override if already set */
  }

  /* Initialize SDL audio subsystem if not already done */
  if (SDL_WasInit(SDL_INIT_AUDIO) == 0) {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
      printf("Warning: SDL_InitSubSystem(AUDIO) failed: %s\n", SDL_GetError());
      printf("         AY8910 will run silently\n");
      ay8910_initialised = true;
      return 0;
    }
  }

  /* Configure SDL audio: mono, 8-bit unsigned, 22050 Hz */
  SDL_AudioSpec want, have;
  SDL_zero(want);
  want.freq = PLAYBACK_FREQUENCY;
  want.format = AUDIO_U8;
  want.channels = 1;
  want.samples = AUDIO_BUF_SIZE;
  want.callback = audio_callback;
  want.userdata = NULL;

  audio_device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
  if (audio_device == 0) {
    printf("Warning: SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
    printf("         AY8910 will run silently (this is normal in WSL1 or headless environments)\n");
    /* Continue anyway - emulator works without sound */
  } else {
    /* Start playback */
    SDL_PauseAudioDevice(audio_device, 0);
    printf("AY8910 audio initialized: %d Hz, %d-bit, %d channel(s)\n",
           have.freq,
           SDL_AUDIO_BITSIZE(have.format),
           have.channels);
  }

  system_register_memory_mapped_device(0xbc00, 0xbc01, ay8910_read_callback, ay8910_write_callback, false);
  system_register_memory_mapped_device(0xbc02, 0xbc03, ay8910_read_callback, ay8910_write_callback, false);

  ay8910_initialised = true;
  return 0;
}

void ay8910_reset(uint8_t bank, uint16_t address) {
  (void)bank;
  (void)address;

  if (!ay8910_initialised)
    return;

  reset_chip(0);
  reset_chip(1);
}

void ay8910_close(void) {
  if (!ay8910_initialised)
    return;

  if (audio_device != 0) {
    SDL_CloseAudioDevice(audio_device);
    audio_device = 0;
  }

  ay8910_initialised = false;
}

