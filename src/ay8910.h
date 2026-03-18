#ifndef __AY8910_H__
#define __AY8910_H__

#include "system.h"
#include <stdbool.h>
#include <stdint.h>

/* AY8910 register indices */
#define AY_AFINE    0x00
#define AY_ACOARSE  0x01
#define AY_BFINE    0x02
#define AY_BCOARSE  0x03
#define AY_CFINE    0x04
#define AY_CCOARSE  0x05
#define AY_NOISEPER 0x06
#define AY_ENABLE   0x07
#define AY_AVOL     0x08
#define AY_BVOL     0x09
#define AY_CVOL     0x0a
#define AY_EFINE    0x0b
#define AY_ECOARSE  0x0c
#define AY_ESHAPE   0x0d
#define AY_PORTA    0x0e
#define AY_PORTB    0x0f

/* Chip clock */
#define AY8910_CLOCK 750000000

/* Convert signed sample (-128..127) to unsigned (0..255) */
#define AUDIO_CONV(A) (128 + (A))

/* Port handler signature */
typedef struct ay8910_t ay8910_t;

/* Port handler: called on read/write of AY_PORTA / AY_PORTB.
 * iswrite: 1 = write, 0 = read.  val: value written (ignored on read).
 * Should store/return the value via PSG->Regs[port]. */
typedef uint8_t (*ay8910_port_handler_t)(ay8910_t* psg, int port, int iswrite, uint8_t val);

/* Internal chip state - one per emulated AY8910 */
typedef struct ay8910_t {
    uint8_t* buffer;               /* sample output buffer */
    ay8910_port_handler_t port[2]; /* port A/B handler callbacks */
    uint8_t regs[16];              /* hardware registers */

    /* Oscillator state */
    int inc_0, inc_1, inc_2;
    int counter_0, counter_1, counter_2;

    /* Envelope state */
    int inc_env, count_env, envelope;

    /* Noise state */
    int inc_noise, count_noise;
    int state_noise, noise_gen;

    /* Cached volume values (recomputed each update) */
    int volume_0, volume_1, volume_2, volume_noise;
} ay8910_t;

/* Lifecycle - called via system_devices table */
extern int ay8910_initialise(uint8_t bank, uint16_t address, uint16_t param, char* identifier);
extern void ay8910_reset(uint8_t bank, uint16_t address);
extern void ay8910_close(void);

/* Register access - called by VIA port handlers */
extern void ay8910_write_reg(int n, int r, int v);
extern uint8_t ay8910_read_reg(int n, int r);

/* Port handler registration */
extern void ay8910_set_port_handler(int n, int port, ay8910_port_handler_t func);

#endif /* __AY8910_H__ */
