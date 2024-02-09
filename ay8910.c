#include <SDL.h>
#include "ay8910.h"

#define NUM_PLAY_NOTIFICATIONS      4

#define PLAYBACK_FREQUENCY          22050 //Hz
#define BUFFER_TIME                 100 //mS
#define BUFFER_BYTES                (PLAYBACK_FREQUENCY*BUFFER_TIME/1000)
#define WRITE_TIME                  50 //mS
#define WRITE_BYTES                 (PLAYBACK_FREQUENCY*WRITE_TIME/1000)
#define UPDATES_PER_SEC             (1000*NUM_PLAY_NOTIFICATIONS/BUFFER_TIME)
#define UPDATES_PER_SEC_DX3         (1000*2/BUFFER_TIME)

static LPDIRECTSOUND lpDS = NULL;
static LPDIRECTSOUNDBUFFER lpDSBStreamBuffer = NULL;
static LPDIRECTSOUNDNOTIFY lpDirectSoundNotify = NULL;
static WAVEFORMATEX WaveFormat;
static DSBUFFERDESC dsbd;
static HANDLE hNotifyEvent[2];
static DWORD dwNextWriteOffset = 0;
static BOOL bClosing = FALSE;
static DWORD dwNotifySize;
static HANDLE hThread;

static int AYSoundRate;     /* Output rate (Hz) */
static int AYBufSize;       /* size of sound buffer, in samples */
static int AYNumChips;      /* total # of PSG's emulated */
static AY8910 *AYPSG;       /* array of PSG's */
static HWND hWinMain = NULL;
BOOL AY8910_Initialised = FALSE;
static BOOL bCantInitialise = FALSE;
static BOOL bThreadRunning = FALSE;
static BOOL bUseNotify = FALSE;

/*
** allocate buffers and clear registers for one of the emulated
** AY8910 chips.
*/
static int _AYInitChip(int num, SAMPLE *buf)
{
    AY8910 *PSG = &(AYPSG[num]);
    PSG->UserBuffer = 0;

    if (buf)
    {
        PSG->Buf = buf;
        PSG->UserBuffer = 1;
    }
    else
    {
        if ((PSG->Buf = (SAMPLE *)malloc(AYBufSize)) == NULL)
        {
            return -1;
        }
    }

    PSG->Port[0] = PSG->Port[1] = NULL;
    AYResetChip(num);
    return 0;
}

/*
** release storage for a chip
*/
static void _AYFreeChip(int num)
{
    AY8910 *PSG = &(AYPSG[num]);

    if (PSG->Buf && !PSG->UserBuffer)
    {
        free(PSG->Buf);
    }

    PSG->Buf = NULL;
}

/*
** Initialize AY8910 emulator(s).
**
** 'num' is the number of virtual AY8910's to allocate
** 'rate' is sampling rate and 'bufsiz' is the size of the
** buffer that should be updated at each interval
*/
int AYInit(int num, int rate, int bufsiz, ...)
{
    int i;
    va_list ap;
    SAMPLE *userbuffer;
    int moreargs = 1;
    va_start(ap, bufsiz);

    if (AYPSG)
    {
        return (-1);    /* duplicate init. */
    }

    AYNumChips = num;
    AYSoundRate = rate;
    AYBufSize = bufsiz;
    AYPSG = (AY8910 *)malloc(sizeof(AY8910) * AYNumChips);

    if (AYPSG == NULL)
    {
        return (0);
    }

    for (i = 0 ; i < AYNumChips; i++)
    {
        if (moreargs)
        {
            userbuffer = va_arg(ap, SAMPLE *);
        }

        if (userbuffer == NULL)
        {
            moreargs = 0;
        }

        userbuffer = NULL;

        if (_AYInitChip(i, userbuffer) < 0)
        {
            int j;

            for (j = 0 ; j < i ; j ++)
            {
                _AYFreeChip(j);
            }

            return (-1);
        }
    }

    return (0);
}

void AYShutdown()
{
    int i;

    if (!AYPSG)
    {
        return;
    }

    for (i = 0 ; i < AYNumChips ; i++)
    {
        _AYFreeChip(i);
    }

    free(AYPSG);
    AYPSG = NULL;
    AYSoundRate = AYBufSize = 0;
}

/*
** reset all chip registers.
*/
void AYResetChip(int num)
{
    int i;
    AY8910 *PSG = &(AYPSG[num]);

    if (!AY8910_Initialised)
    {
        return;
    }

    memset(PSG->Buf, '\0', AYBufSize);

    /* initialize hardware registers */
    for (i = 0; i < 16; i++)
    {
        PSG->Regs[i] = AUDIO_CONV(0);
    }

    /*
        PSG->Regs[AY_ENABLE] = 077;
        PSG->Regs[AY_AVOL] = 8;
        PSG->Regs[AY_BVOL] = 8;
        PSG->Regs[AY_CVOL] = 8;
    */
    PSG->NoiseGen = 1;
    PSG->Envelope = 15;
    PSG->StateNoise = 0;
    PSG->Incr0 = PSG->Incr1 = PSG->Incr2 = PSG->Increnv = PSG->Incrnoise = 0;

    for (i = 0; i < 16; i++)
    {
        AYWriteReg(num, i, 0);
    }
}

static unsigned char _AYEnvForms[16][32] =
{
    {
        15, 14, 13, 12, 11, 10,  9,  8,  7,  6,  5,  4,  3,  2,  1,  0,
        0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0
    },
    {
        15, 14, 13, 12, 11, 10,  9,  8,  7,  6,  5,  4,  3,  2,  1,  0,
        0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0
    },
    {
        15, 14, 13, 12, 11, 10,  9,  8,  7,  6,  5,  4,  3,  2,  1,  0,
        0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0
    },
    {
        15, 14, 13, 12, 11, 10,  9,  8,  7,  6,  5,  4,  3,  2,  1,  0,
        0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0
    },
    {
        0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
        0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0
    },
    {
        0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
        0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0
    },
    {
        0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
        0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0
    },
    {
        0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
        0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0
    },
    {
        15, 14, 13, 12, 11, 10,  9,  8,  7,  6,  5,  4,  3,  2,  1,  0,
        15, 14, 13, 12, 11, 10,  9,  8,  7,  6,  5,  4,  3,  2,  1,  0
    },
    {
        15, 14, 13, 12, 11, 10,  9,  8,  7,  6,  5,  4,  3,  2,  1,  0,
        0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0
    },
    {
        15, 14, 13, 12, 11, 10,  9,  8,  7,  6,  5,  4,  3,  2,  1,  0,
        0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15
    },
    {
        15, 14, 13, 12, 11, 10,  9,  8,  7,  6,  5,  4,  3,  2,  1,  0,
        15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15
    },
    {
        0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
        0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15
    },
    {
        0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
        15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15
    },
    {
        0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
        15, 14, 13, 12, 11, 10,  9,  8,  7,  6,  5,  4,  3,  2,  1,  0
    },
    {
        0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
        0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0
    }
};


/* write a register on AY8910 chip number 'n' */
void AYWriteReg(int n, int r, int v)
{
    AY8910 *PSG = &(AYPSG[n]);

    if ((!AY8910_Initialised) && (!bCantInitialise))
    {
        AY8910_Init(hWinMain);
    }

    if (!AY8910_Initialised)
    {
        return;
    }

    PSG->Regs[r] = v;

    switch (r)
    {
    case AY_AVOL:
    case AY_BVOL:
    case AY_CVOL:
        PSG->Regs[r] &= 0x1F;   /* mask volume */
        break;

    case AY_EFINE:
    case AY_ECOARSE:
        /* fall through */
        break;

    case AY_ESHAPE:
        PSG->Countenv = 0;
        PSG->Regs[AY_ESHAPE] &= 0xF;
        break;

    case AY_PORTA:
        if (PSG->Port[0])
        {
            (PSG->Port[0])(PSG, AY_PORTA, 1, (byte)v);
        }

        break;

    case AY_PORTB:
        if (PSG->Port[1])
        {
            (PSG->Port[1])(PSG, AY_PORTB, 1, (byte)v);
        }

        break;
    }
}

byte AYReadReg(int n, int r)
{
    AY8910 *PSG = &(AYPSG[n]);

    if (!AY8910_Initialised)
    {
        return (0xff);
    }

    switch (r)
    {
    case AY_PORTA:
        if (PSG->Port[0])
        {
            (PSG->Port[0])(PSG, AY_PORTA, 0, 0);
        }

        break;

    case AY_PORTB:
        if (PSG->Port[1])
        {
            (PSG->Port[1])(PSG, AY_PORTB, 0, 0);
        }

        break;
    }

    return PSG->Regs[r];
}

static void _AYUpdateChip(int num, DWORD pdwBytes)
{
    AY8910 *PSG = &(AYPSG[num]);
    int v = 0, x;
    int c0, c1, l0, l1, l2;
    DWORD dw;
    BYTE *lpb;
    x = (PSG->Regs[AY_AFINE] + ((unsigned)(PSG->Regs[AY_ACOARSE] & 0xF) << 8));
    PSG->Incr0 = x ? AY8910_CLOCK / AYSoundRate * 4 / x : 0;
    x = (PSG->Regs[AY_BFINE] + ((unsigned)(PSG->Regs[AY_BCOARSE] & 0xF) << 8));
    PSG->Incr1 = x ? AY8910_CLOCK / AYSoundRate * 4 / x : 0;
    x = (PSG->Regs[AY_CFINE] + ((unsigned)(PSG->Regs[AY_CCOARSE] & 0xF) << 8));
    PSG->Incr2 = x ? AY8910_CLOCK / AYSoundRate * 4 / x : 0;
    x = PSG->Regs[AY_NOISEPER] & 0x1F;
    PSG->Incrnoise = AY8910_CLOCK / AYSoundRate * 4 / (x ? x : 1);
    x = (PSG->Regs[AY_EFINE] + ((unsigned)PSG->Regs[AY_ECOARSE] << 8));
    PSG->Increnv = x ? AY8910_CLOCK / AYSoundRate * 4 / x * pdwBytes : 0;
    PSG->Envelope = _AYEnvForms[PSG->Regs[AY_ESHAPE]][(PSG->Countenv >> 16) & 0x1F];

    if ((PSG->Countenv += PSG->Increnv) & 0xFFE00000)
    {
        switch (PSG->Regs[AY_ESHAPE])
        {
        case 8:
        case 10:
        case 12:
        case 14:
            PSG->Countenv -= 0x200000;
            break;

        default:
            PSG->Countenv = 0x100000;
            PSG->Increnv = 0;
        }
    }

    PSG->Vol0 = (PSG->Regs[AY_AVOL] < 16) ? PSG->Regs[AY_AVOL] : PSG->Envelope;
    PSG->Vol1 = (PSG->Regs[AY_BVOL] < 16) ? PSG->Regs[AY_BVOL] : PSG->Envelope;
    PSG->Vol2 = (PSG->Regs[AY_CVOL] < 16) ? PSG->Regs[AY_CVOL] : PSG->Envelope;
    PSG->Volnoise = (
                        ((PSG->Regs[AY_ENABLE] & 010) ? 0 : PSG->Vol0) +
                        ((PSG->Regs[AY_ENABLE] & 020) ? 0 : PSG->Vol1) +
                        ((PSG->Regs[AY_ENABLE] & 040) ? 0 : PSG->Vol2)) / 2;
    PSG->Vol0 = (PSG->Regs[AY_ENABLE] & 001) ? 0 : PSG->Vol0;
    PSG->Vol1 = (PSG->Regs[AY_ENABLE] & 002) ? 0 : PSG->Vol1;
    PSG->Vol2 = (PSG->Regs[AY_ENABLE] & 004) ? 0 : PSG->Vol2;
    lpb = PSG->Buf;

    for (dw = 0; dw < pdwBytes; ++dw)
    {
        /*
        ** These strange tricks are needed for getting rid
        ** of nasty interferences between sampling frequency
        ** and "rectangular sound" (which is also the output
        ** of real AY-3-8910) we produce.
        */
        c0 = PSG->Counter0;
        c1 = PSG->Counter0 + PSG->Incr0;
        l0 = ((c0 & 0x8000) ? -16 : 16);

        if ((c0 ^ c1) & 0x8000)
        {
            l0 = l0 * (0x8000 - (c0 & 0x7FFF) - (c1 & 0x7FFF)) / PSG->Incr0;
        }

        PSG->Counter0 = c1 & 0xFFFF;
        c0 = PSG->Counter1;
        c1 = PSG->Counter1 + PSG->Incr1;
        l1 = ((c0 & 0x8000) ? -16 : 16);

        if ((c0 ^ c1) & 0x8000)
        {
            l1 = l1 * (0x8000 - (c0 & 0x7FFF) - (c1 & 0x7FFF)) / PSG->Incr1;
        }

        PSG->Counter1 = c1 & 0xFFFF;
        c0 = PSG->Counter2;
        c1 = PSG->Counter2 + PSG->Incr2;
        l2 = ((c0 & 0x8000) ? -16 : 16);

        if ((c0 ^ c1) & 0x8000)
        {
            l2 = l2 * (0x8000 - (c0 & 0x7FFF) - (c1 & 0x7FFF)) / PSG->Incr2;
        }

        PSG->Counter2 = c1 & 0xFFFF;
        PSG->Countnoise &= 0xFFFF;

        if ((PSG->Countnoise += PSG->Incrnoise) & 0xFFFF0000)
        {
            /*
            ** The following code is a random bit generator :)
            */
            PSG->StateNoise =
                ((PSG->NoiseGen <<= 1) & 0x80000000
                 ? PSG->NoiseGen ^= 0x00040001 : PSG->NoiseGen) & 1;
        }

        *lpb++ = AUDIO_CONV(
                     (l0 * PSG->Vol0 + l1 * PSG->Vol1 + l2 * PSG->Vol2) / 6 +
                     (PSG->StateNoise ? PSG->Volnoise : -PSG->Volnoise));
    }
}

/*
** called to update all chips
*/
void AYUpdate(DWORD pdwBytes)
{
    int i;

    for (i = 0 ; i < AYNumChips; i++)
    {
        _AYUpdateChip(i, pdwBytes);
    }
}


/*
** return the buffer into which AYUpdate() has just written it's sample
** data
*/
SAMPLE *AYBuffer(int n)
{
    return AYPSG[n].Buf;
}

void AYSetBuffer(int n, SAMPLE *buf)
{
    AYPSG[n].Buf = buf;
}

/*
** set a port handler function to be called when AYWriteReg() or AYReadReg()
** is called for register AY_PORTA or AY_PORTB.
**
*/
void AYSetPortHandler(int n, int port, ay8910_port_Handler func)
{
    port -= AY_PORTA;

    if (port > 1 || port < 0)
    {
        return;
    }

    AYPSG[n].Port[port] = func;
}



/*
** This thread is created when DirectX 5 or later is used
*/
DWORD AY8910_HandleNotifications(LPVOID lpvoid)
{
    DWORD hObject;
    LPBYTE lpWrite1;
    LPBYTE lpb;
    SAMPLE *lpSample1;
    SAMPLE *lpSample2;
    DWORD dwWrite1 = 0;
    LPBYTE lpWrite2;
    DWORD dwWrite2 = 0;
    HRESULT hResult;
    DWORD n;
    bThreadRunning = TRUE;
    SetThreadPriority(hThread, THREAD_PRIORITY_HIGHEST);

    while (((hObject = WaitForMultipleObjects(2, hNotifyEvent, FALSE, INFINITE)) != WAIT_FAILED) && (!bClosing))
    {
        switch (hObject - WAIT_OBJECT_0)
        {
        case 0:     // Move data into buffer
            hResult = lpDSBStreamBuffer->lpVtbl->Lock(lpDSBStreamBuffer, dwNextWriteOffset, dwNotifySize, &lpWrite1, &dwWrite1, &lpWrite2, &dwWrite2, 0);

            if (hResult != DS_OK)
            {
                bThreadRunning = FALSE;
                return (1);
            }

            lpSample1 = AYBuffer(0);
            lpSample2 = AYBuffer(1);

            for (lpb = lpWrite1, n = 0; n < dwWrite1; n++)
            {
                *(lpb++) = (BYTE)(((WORD)(*lpSample1++) + (WORD)(*lpSample2++)) / 2);
            }

            for (lpb = lpWrite2, n = 0; n < dwWrite2; n++)
            {
                *(lpb++) = (BYTE)(((WORD)(*lpSample1++) + (WORD)(*lpSample2++)) / 2);
            }

            hResult = lpDSBStreamBuffer->lpVtbl->Unlock(lpDSBStreamBuffer, (LPVOID)lpWrite1, dwWrite1, (LPVOID)lpWrite2, dwWrite2);
            dwNextWriteOffset += (dwWrite1 + dwWrite2);

            if (dwNextWriteOffset >= dsbd.dwBufferBytes)
            {
                dwNextWriteOffset -= dsbd.dwBufferBytes;
            }

            AYUpdate(AYBufSize);
            break;

        case 1:     // Stop
            bClosing = TRUE;
            break;
        }

        if (bClosing)
        {
            break;
        }
    }

    if (lpDirectSoundNotify)
    {
        lpDirectSoundNotify->lpVtbl->Release(lpDirectSoundNotify);
    }

    lpDirectSoundNotify = NULL;

    if (lpDSBStreamBuffer)
    {
        lpDSBStreamBuffer->lpVtbl->Release(lpDSBStreamBuffer);
    }

    lpDSBStreamBuffer = NULL;
    bThreadRunning = FALSE;
    return (0);
}


/*
** This thread is created when DirectX 4 or earlier is used
*/
DWORD AY8910_SoundThread(LPVOID lpvoid)
{
    LPBYTE lpWrite1;
    register LPBYTE lpb;
    register SAMPLE *lpSample1;
    register SAMPLE *lpSample2;
    DWORD dwWrite1 = 0;
    HRESULT hResult;
    register DWORD n;
    DWORD dwPlay;
    DWORD dwWrite;
    bThreadRunning = TRUE;

    while (!bClosing)
    {
        AYUpdate(AYBufSize);
        lpSample1 = AYBuffer(0);
        lpSample2 = AYBuffer(1);
        n = dwWrite1;
        SetThreadPriority(hThread, THREAD_PRIORITY_NORMAL);

        while (TRUE)
        {
            lpDSBStreamBuffer->lpVtbl->GetCurrentPosition(lpDSBStreamBuffer, &dwPlay, &dwWrite);

            if ((dwNextWriteOffset == 0) && (dwPlay >= BUFFER_BYTES / 2))
            {
                break;
            }

            if ((dwNextWriteOffset == BUFFER_BYTES / 2) && (dwPlay < BUFFER_BYTES / 2))
            {
                break;
            }

            if (bClosing)
            {
                break;
            }

            Sleep(0);
        }

        if (bClosing)
        {
            break;
        }

        SetThreadPriority(hThread, THREAD_PRIORITY_HIGHEST);
        hResult = lpDSBStreamBuffer->lpVtbl->Lock(lpDSBStreamBuffer, dwNextWriteOffset, BUFFER_BYTES / 2, &lpWrite1, &dwWrite1, NULL, NULL, 0);

        if (hResult != DS_OK)
        {
            break;
        }

        for (lpb = lpWrite1; n != 0; n--)
        {
            *(lpb++) = (BYTE)(((WORD)(*lpSample1++) + (WORD)(*lpSample2++)) / 2);
        }

        hResult = lpDSBStreamBuffer->lpVtbl->Unlock(lpDSBStreamBuffer, (LPVOID)lpWrite1, dwWrite1, NULL, 0);

        if (hResult != DS_OK)
        {
            break;
        }

        dwNextWriteOffset = BUFFER_BYTES / 2 - dwNextWriteOffset;
    }

    if (lpDSBStreamBuffer)
    {
        lpDSBStreamBuffer->lpVtbl->Release(lpDSBStreamBuffer);
    }

    lpDSBStreamBuffer = NULL;
    bThreadRunning = FALSE;
    return (0);
}



int AY8910_InitDSound(HWND hWndMain)
{
    HRESULT dsRetVal;
    dsRetVal = DirectSoundCreate(NULL, &lpDS, NULL);

    if (dsRetVal != DS_OK)
    {
        return (AY_CREATE_FAILED);
    }

    dsRetVal = lpDS->lpVtbl->SetCooperativeLevel(lpDS, hWndMain, DSSCL_NORMAL);

    if (dsRetVal != DS_OK)
    {
        return (AY_SET_COOP_LEVEL_FAILED);
    }

    return (AY_OK);
}


int AY8910_SetupStreamBuffer()
{
    HRESULT dsRetVal;
    WaveFormat.wFormatTag = WAVE_FORMAT_PCM;
    WaveFormat.nChannels = 1;
    WaveFormat.nSamplesPerSec = PLAYBACK_FREQUENCY;
    WaveFormat.wBitsPerSample = 8;
    WaveFormat.nBlockAlign = WaveFormat.nChannels * WaveFormat.wBitsPerSample / 8;
    WaveFormat.nAvgBytesPerSec = WaveFormat.nBlockAlign * WaveFormat.nSamplesPerSec;
    WaveFormat.cbSize = 0;
    //Create the secondary DirectSoundBuffer object to receive our sound data.
    memset(&dsbd, 0, sizeof(DSBUFFERDESC));
    dsbd.dwSize = sizeof(DSBUFFERDESC);
    // Use new GetCurrentPosition() accuracy (DirectX 2 feature)
    dsbd.dwFlags = DSBCAPS_CTRLPOSITIONNOTIFY | DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_STICKYFOCUS | DSBCAPS_CTRLFREQUENCY | DSBCAPS_CTRLPAN | DSBCAPS_CTRLVOLUME;
    dwNotifySize = BUFFER_BYTES / NUM_PLAY_NOTIFICATIONS;
    dsbd.dwBufferBytes = dwNotifySize * NUM_PLAY_NOTIFICATIONS;
    //Set Format properties
    dsbd.lpwfxFormat = &WaveFormat;
    dsRetVal = lpDS->lpVtbl->CreateSoundBuffer(lpDS, &dsbd, &lpDSBStreamBuffer, NULL);

    if (dsRetVal != DS_OK)
    {
        return (AY_CREATE_SBUF_FAILED);
    }

    dwNextWriteOffset = 0;
    return (AY_OK);
}



int AY8910_SetupNotification()
{
    DWORD dwThreadId;
    static DSBPOSITIONNOTIFY dsbPosNotify[NUM_PLAY_NOTIFICATIONS + 1];
    int n;
    HRESULT dsRetVal;
    // now get the pointer to the notification interface.
    dsRetVal = IDirectSoundNotify_QueryInterface(lpDSBStreamBuffer, &IID_IDirectSoundNotify, &((LPVOID)lpDirectSoundNotify));

    if (dsRetVal != DS_OK)
    {
        return (AY_QINOTIFY_FAILED);
    }

    // Create the 2 events. One for Play one for stop.
    hNotifyEvent[0] = CreateEvent(NULL, FALSE, FALSE, NULL);
    hNotifyEvent[1] = CreateEvent(NULL, FALSE, FALSE, NULL);
    // setup the first one.
    dsbPosNotify[0].dwOffset = dwNotifySize;
    dsbPosNotify[0].hEventNotify = hNotifyEvent[0];

    for (n = 1; n < NUM_PLAY_NOTIFICATIONS; n++)
    {
        dsbPosNotify[n].dwOffset = dsbPosNotify[n - 1].dwOffset + dwNotifySize;
        dsbPosNotify[n].hEventNotify = hNotifyEvent[0];
    }

    dsbPosNotify[n - 1].dwOffset -= 1;
    // set the stop notification.
    dsbPosNotify[n].dwOffset = DSBPN_OFFSETSTOP;
    dsbPosNotify[n].hEventNotify = hNotifyEvent[1];

    // setup notification
    if (lpDirectSoundNotify->lpVtbl->SetNotificationPositions(lpDirectSoundNotify, NUM_PLAY_NOTIFICATIONS + 1, dsbPosNotify) != DS_OK)
    {
        return (AY_SET_NOTIFICATION_POSITIONS_FAILED);
    }

    // Now create the thread to wait on the events created.
    if ((hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)AY8910_HandleNotifications, NULL, 0, &dwThreadId)) == NULL)
    {
        return (AY_CREATE_NOTIFICATION_HANDLER_FAILED);
    }

    return (AY_OK);
}



int AY8910_Init(HWND hWndMain)
{
    int nResult;
    DWORD dwThreadId;
    hWinMain = hWndMain;
    bCantInitialise = TRUE;
    bClosing = FALSE;
    bUseNotify = FALSE;

    if (!AY8910_Initialised)
    {
        if ((nResult = AY8910_InitDSound(hWndMain)) != AY_OK)
        {
            return (nResult);
        }

        if ((nResult = AY8910_SetupStreamBuffer()) != AY_OK)
        {
            return (nResult);
        }

        /* Try to set up notifications - if this fails, set up the more primative method */
        if ((nResult = AY8910_SetupNotification()) == AY_OK)
        {
            AYInit(2, PLAYBACK_FREQUENCY, PLAYBACK_FREQUENCY / UPDATES_PER_SEC);
            bUseNotify = TRUE;
        }
        else
        {
            AYInit(2, PLAYBACK_FREQUENCY, PLAYBACK_FREQUENCY / UPDATES_PER_SEC_DX3);

            if ((hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)AY8910_SoundThread, NULL, 0, &dwThreadId)) == NULL)
            {
                return (AY_INIT_FAILED);
            }
        }

        if (lpDSBStreamBuffer->lpVtbl->SetCurrentPosition(lpDSBStreamBuffer, 0) != DS_OK)
        {
            return (AY_INIT_FAILED);
        }

        if (lpDSBStreamBuffer->lpVtbl->Play(lpDSBStreamBuffer, 0, 0, DSBPLAY_LOOPING) != DS_OK)
        {
            return (AY_PLAY_FAILED);
        }

        AY8910_Initialised = TRUE;
        bCantInitialise = FALSE;
        AYResetChip(0);
        AYResetChip(1);
    }

    return (AY_OK);
}



void AY8910_Close()
{
    int n;

    if (AY8910_Initialised)
    {
        if (bUseNotify)
        {
            CloseHandle(hNotifyEvent[0]);
            CloseHandle(hNotifyEvent[1]);
            hNotifyEvent[0] = hNotifyEvent[1] = (HANDLE)NULL;
        }

        bClosing = TRUE;

        for (n = 0; (n < 200) && bThreadRunning; n++)
        {
            Sleep(10);
        }

        lpDS->lpVtbl->Release(lpDS);
        AY8910_Initialised = FALSE;
    }
}
