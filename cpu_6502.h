#ifndef __CPU_6502_H__
#define __CPU_6502_H__

#include <stdint.h>
#include "system.h"

#define PSW_C       (1 << 0)
#define PSW_Z       (1 << 1)
#define PSW_I       (1 << 2)
#define PSW_D       (1 << 3)
#define PSW_B       (1 << 4)
#define PSW_V       (1 << 6)
#define PSW_N       (1 << 7)

extern void cpu_6502_reset(uint8_t bank, uint16_t address);
extern void cpu_6502_execute(int timer_ticks);
extern void cpu_6502_assert_nmi();
extern void cpu_6502_assert_irq();
extern void cpu_6502_set_delayed_nmi();
extern void cpu_6502_continue(uint16_t pc, uint8_t a, uint8_t ix, uint8_t iy, uint8_t sp, uint8_t psw);
extern int cpu_6502_initialise(uint8_t bank, uint16_t address, uint16_t param, char *identifier);
extern uint16_t cpu_6502_get_pc();

#endif // __CPU_6502_H__
