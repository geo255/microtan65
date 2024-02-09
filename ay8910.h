#ifndef __AY8910_H__
#define __AY8910_H__

#include <stdbool.h>
#include <stdint.h>

#define AY_AFINE	0x00
#define AY_ACOARSE	0x01
#define AY_BFINE	0x02
#define AY_BCOARSE	0x03
#define AY_CFINE	0x04
#define AY_CCOARSE	0x05
#define AY_NOISEPER	0x06
#define AY_ENABLE	0x07
#define AY_AVOL		0x08
#define AY_BVOL		0x09
#define AY_CVOL		0x0a
#define AY_EFINE	0x0b
#define AY_ECOARSE	0x0c
#define AY_ESHAPE	0x0d
#define AY_PORTA	0x0e
#define AY_PORTB	0x0f

#define AY8910_CLOCK 750000000

#define AUDIO_CONV(A) (128+(A))	/* use this macro for signed samples */

typedef uint8_t (*ay8910_port_Handler)(AY8910 *, int port, int iswrite, byte val);

/* here's the virtual AY8910 ... */
typedef struct ay8910_t
{
    uint8_t *buffer;
    ay8910_port_Handler port[2];
    uint8_t Regs[16];

    /* state variables */
    int Incr0, Incr1, Incr2;
    int Increnv, Incrnoise;
    int StateNoise, NoiseGen;
    int Counter0, Counter1, Counter2, Countenv, Countnoise;
    int Vol0, Vol1, Vol2, Volnoise, Envelope;
} ay8910;


extern int AYInit(int num, int rate, int bufsiz, ...);
extern void AYShutdown(void);
extern void AYResetChip(int num);
extern void AYUpdate(DWORD pdwBytes);
extern void AYWriteReg(int n, int r, int v);
extern byte AYReadReg(int n, int r);
extern SAMPLE *AYBuffer(int n);
extern void AYSetBuffer(int n, SAMPLE *buf);
extern void AYSetPortHandler(int n, int port, ay8910_port_Handler func);
extern BOOL AY8910_Initialised;
extern int AY8910_Init(HWND hWndMain);
extern void AY8910_Close();

#endif // __AY8910_H__
