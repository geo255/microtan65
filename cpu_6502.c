#include <stdbool.h>
#include <stdint.h>

#include "cpu_6502.h"
#include "display.h"
#include "system.h"
#include "function_return_codes.h"
#include "via_6522.h"

typedef struct
{
    uint32_t ticks;
    void (*instruction)();
    void (*address_mode)();
} instruction_t;
static const instruction_t instruction_table[256];

static uint8_t opcode;
static uint8_t reg_a;
static uint8_t reg_x;
static uint8_t reg_y;
static uint8_t reg_psw;
static uint8_t reg_sp;
static uint16_t reg_pc;
static bool flag_irq;
static bool flag_nmi;
static uint16_t save_pc;
static uint8_t save_carry;
static uint32_t instruction_ticks;
static uint8_t byte_value;
static uint16_t word_value;
static int sum;
static int delayed_nmi_counter = 0;
static int instruction_length;

uint16_t cpu_6502_get_pc()
{
    return reg_pc;
}

/*
** Addressing modes
*/
static void implied()
{
    instruction_length = 1;
}

static void immediate()
{
    save_pc = reg_pc++;
    instruction_length = 2;
}

static void absolute()
{
    save_pc = system_read_memory(reg_pc) + (system_read_memory(reg_pc + 1) << 8);
    reg_pc++;
    reg_pc++;
    instruction_length = 3;
}

static void relative()
{
    instruction_length = 2;
    save_pc = system_read_memory(reg_pc++);

    if (save_pc & 0x80)
    {
        save_pc -= 0x100;
    }

    if ((save_pc >> 8) != (reg_pc >> 8))
    {
        instruction_ticks++;
    }
}

static void indirect()
{
    instruction_length = 3;
    word_value = system_read_memory(reg_pc) + (system_read_memory(reg_pc + 1) << 8);
    save_pc = system_read_memory(word_value) + (system_read_memory(word_value + 1) << 8);
    reg_pc++;
    reg_pc++;
}

static void absolute_x()
{
    instruction_length = 3;
    save_pc = system_read_memory(reg_pc) + (system_read_memory(reg_pc + 1) << 8);
    reg_pc++;
    reg_pc++;

    if (instruction_table[opcode].ticks == 4)
    {
        if ((save_pc >> 8) != ((save_pc + reg_x) >> 8))
        {
            instruction_ticks++;
        }
    }

    save_pc += reg_x;
}

static void absolute_y()
{
    instruction_length = 3;
    save_pc = system_read_memory(reg_pc) + (system_read_memory(reg_pc + 1) << 8);
    reg_pc++;
    reg_pc++;

    if (instruction_table[opcode].ticks == 4)
    {
        if ((save_pc >> 8) != ((save_pc + reg_y) >> 8))
        {
            instruction_ticks++;
        }
    }

    save_pc += reg_y;
}

static void zero_page()
{
    instruction_length = 2;
    save_pc = system_read_memory(reg_pc++);
}

static void zero_page_x()
{
    instruction_length = 2;
    save_pc = system_read_memory(reg_pc++) + reg_x;
    save_pc &= 0x00ff;
}

static void zero_page_y()
{
    instruction_length = 2;
    save_pc = system_read_memory(reg_pc++) + reg_y;
    save_pc &= 0x00ff;
}

static void indirect_x()
{
    instruction_length = 4;
    byte_value = system_read_memory(reg_pc++) + reg_x;
    save_pc = system_read_memory(byte_value) + (system_read_memory(byte_value + 1) << 8);
}

static void indirect_y()
{
    instruction_length = 4;
    byte_value = system_read_memory(reg_pc++);
    save_pc = system_read_memory(byte_value) + (system_read_memory(byte_value + 1) << 8);

    if (instruction_table[opcode].ticks == 5)
    {
        if ((save_pc >> 8) != ((save_pc + reg_y) >> 8))
        {
            instruction_ticks++;
        }
    }

    save_pc += reg_y;
}

static void indirect_absolute_x()
{
    instruction_length = 3;
    word_value = system_read_memory(reg_pc) + (system_read_memory(reg_pc + 1) << 8) + reg_x;
    save_pc = system_read_memory(word_value) + (system_read_memory(word_value + 1) << 8);
}

static void indirect_zero_page()
{
    instruction_length = 2;
    byte_value = system_read_memory(reg_pc++);
    save_pc = system_read_memory(byte_value) + (system_read_memory(byte_value + 1) << 8);
}


/*
** Instructions
*/
static void adc()
{
    instruction_table[opcode].address_mode();
    byte_value = system_read_memory(save_pc);
    save_carry = (reg_psw & PSW_C) ? 1 : 0;

    if (reg_psw & PSW_D)
    {
        int operator_1 = reg_a;
        int store = (operator_1 & 0x0f) + (int)(byte_value & 0x0f) + save_carry;
        reg_a = (store < 0x0a) ? store : (store + 6);
        store = (operator_1 & 0xf0) + (int)(byte_value & 0xf0) + (reg_a & 0xf0);

        if (store < 0)
        {
            reg_psw |= PSW_N;
        }
        else
        {
            reg_psw &= ~PSW_N;
        }

        if (((operator_1 ^ store) & ~(operator_1 ^ byte_value) & 0x80) != 0)
        {
            reg_psw |= PSW_V;
        }
        else
        {
            reg_psw &= ~PSW_V;
        }

        store = (reg_a & 0x0f) | ((store < 0xa0) ? store : (store + 0x60));

        if (store >= 0x100)
        {
            reg_psw |= PSW_C;
        }
        else
        {
            reg_psw &= 0xfe;
        }

        reg_a = store & 0xff;
    }
    else
    {
        sum = ((int) reg_a) + ((int) byte_value) + save_carry;

        if ((sum > 0x7f) || (sum < -0x80))
        {
            reg_psw |= PSW_V;
        }
        else
        {
            reg_psw &= ~PSW_V;
        }

        if (sum & 0xff00)
        {
            reg_psw |= PSW_C;
        }
        else
        {
            reg_psw &= ~PSW_C;
        }

        reg_a = sum & 0xff;

        if (reg_a & 0x80)
        {
            reg_psw |= PSW_N;
        }
        else
        {
            reg_psw &= ~PSW_N;
        }
    }

    instruction_ticks++;

    if (reg_a)
    {
        reg_psw &= ~PSW_Z;
    }
    else
    {
        reg_psw |= PSW_Z;
    }
}

static void and ()
{
    instruction_table[opcode].address_mode();
    byte_value = system_read_memory(save_pc);
    reg_a &= byte_value;

    if (reg_a)
    {
        reg_psw &= 0xfd;
    }
    else
    {
        reg_psw |= 0x02;
    }

    if (reg_a & 0x80)
    {
        reg_psw |= 0x80;
    }
    else
    {
        reg_psw &= 0x7f;
    }
}

static void asl()
{
    instruction_table[opcode].address_mode();
    byte_value = system_read_memory(save_pc);
    reg_psw = (reg_psw & 0xfe) | ((byte_value >> 7) & 0x01);
    byte_value = byte_value << 1;
    system_write_memory(save_pc, byte_value);

    if (byte_value)
    {
        reg_psw &= 0xfd;
    }
    else
    {
        reg_psw |= 0x02;
    }

    if (byte_value & 0x80)
    {
        reg_psw |= 0x80;
    }
    else
    {
        reg_psw &= 0x7f;
    }
}

static void asla()
{
    reg_psw = (reg_psw & 0xfe) | ((reg_a >> 7) & 0x01);
    reg_a = reg_a << 1;

    if (reg_a)
    {
        reg_psw &= 0xfd;
    }
    else
    {
        reg_psw |= 0x02;
    }

    if (reg_a & 0x80)
    {
        reg_psw |= 0x80;
    }
    else
    {
        reg_psw &= 0x7f;
    }
}

static void bcc()
{
    if ((reg_psw & 0x01) == 0)
    {
        instruction_table[opcode].address_mode();
        reg_pc += save_pc;
        instruction_ticks++;
    }
    else
    {
        byte_value = system_read_memory(reg_pc++);
    }
}

static void bcs()
{
    if (reg_psw & 0x01)
    {
        instruction_table[opcode].address_mode();
        reg_pc += save_pc;
        instruction_ticks++;
    }
    else
    {
        byte_value = system_read_memory(reg_pc++);
    }
}

static void beq()
{
    if (reg_psw & 0x02)
    {
        instruction_table[opcode].address_mode();
        reg_pc += save_pc;
        instruction_ticks++;
    }
    else
    {
        byte_value = system_read_memory(reg_pc++);
    }
}

static void bit()
{
    instruction_table[opcode].address_mode();
    byte_value = system_read_memory(save_pc);

    /* non-destrucive logically And between m_bValue and the accumulator
     * and set zero flag */
    if (byte_value & reg_a)
    {
        reg_psw &= 0xfd;
    }
    else
    {
        reg_psw |= 0x02;
    }

    /* set negative and overflow flags from m_bValue */
    reg_psw = (reg_psw & 0x3f) | (byte_value & 0xc0);
}

static void bmi()
{
    if (reg_psw & 0x80)
    {
        instruction_table[opcode].address_mode();
        reg_pc += save_pc;
        instruction_ticks++;
    }
    else
    {
        byte_value = system_read_memory(reg_pc++);
    }
}

static void bne()
{
    if ((reg_psw & 0x02) == 0)
    {
        instruction_table[opcode].address_mode();
        reg_pc += save_pc;
        instruction_ticks++;
    }
    else
    {
        byte_value = system_read_memory(reg_pc++);
    }
}

static void bpl()
{
    if ((reg_psw & 0x80) == 0)
    {
        instruction_table[opcode].address_mode();
        reg_pc += save_pc;
        instruction_ticks++;
    }
    else
    {
        byte_value = system_read_memory(reg_pc++);
    }
}

static void brk()
{
    reg_pc++;
    reg_psw |= 0x14;
    system_write_memory(0x0100 + reg_sp--, (uint8_t)(reg_pc >> 8));
    system_write_memory(0x0100 + reg_sp--, (uint8_t)(reg_pc & 0xff));
    system_write_memory(0x0100 + reg_sp--, reg_psw);
    reg_pc = system_read_memory(0xfffe) + ((uint16_t)system_read_memory(0xffff) << 8);
}

static void bvc()
{
    if ((reg_psw & 0x40) == 0)
    {
        instruction_table[opcode].address_mode();
        reg_pc += save_pc;
        instruction_ticks++;
    }
    else
    {
        byte_value = system_read_memory(reg_pc++);
    }
}

static void bvs()
{
    if (reg_psw & 0x40)
    {
        instruction_table[opcode].address_mode();
        reg_pc += save_pc;
        instruction_ticks++;
    }
    else
    {
        byte_value = system_read_memory(reg_pc++);
    }
}

static void clc()
{
    reg_psw &= 0xfe;
}

static void cld()
{
    reg_psw &= 0xf7;
}

static void cli()
{
    reg_psw &= 0xfb;
}

static void clv()
{
    reg_psw &= 0xbf;
}

static void cmp()
{
    instruction_table[opcode].address_mode();
    byte_value = system_read_memory(save_pc);

    if (reg_a + 0x100 - byte_value > 0xff)
    {
        reg_psw |= 0x01;
    }
    else
    {
        reg_psw &= 0xfe;
    }

    byte_value = reg_a + 0x100 - byte_value;

    if (byte_value)
    {
        reg_psw &= 0xfd;
    }
    else
    {
        reg_psw |= 0x02;
    }

    if (byte_value & 0x80)
    {
        reg_psw |= 0x80;
    }
    else
    {
        reg_psw &= 0x7f;
    }
}

static void cpx()
{
    instruction_table[opcode].address_mode();
    byte_value = system_read_memory(save_pc);

    if (reg_x + 0x100 - byte_value > 0xff)
    {
        reg_psw |= 0x01;
    }
    else
    {
        reg_psw &= 0xfe;
    }

    byte_value = reg_x + 0x100 - byte_value;

    if (byte_value)
    {
        reg_psw &= 0xfd;
    }
    else
    {
        reg_psw |= 0x02;
    }

    if (byte_value & 0x80)
    {
        reg_psw |= 0x80;
    }
    else
    {
        reg_psw &= 0x7f;
    }
}

static void cpy()
{
    instruction_table[opcode].address_mode();
    byte_value = system_read_memory(save_pc);

    if (reg_y + 0x100 - byte_value > 0xff)
    {
        reg_psw |= 0x01;
    }
    else
    {
        reg_psw &= 0xfe;
    }

    byte_value = reg_y + 0x100 - byte_value;

    if (byte_value)
    {
        reg_psw &= 0xfd;
    }
    else
    {
        reg_psw |= 0x02;
    }

    if (byte_value & 0x80)
    {
        reg_psw |= 0x80;
    }
    else
    {
        reg_psw &= 0x7f;
    }
}

static void dec()
{
    instruction_table[opcode].address_mode();
    system_write_memory(save_pc, byte_value = system_read_memory(save_pc) - 1);

    if (byte_value)
    {
        reg_psw &= 0xfd;
    }
    else
    {
        reg_psw |= 0x02;
    }

    if (byte_value & 0x80)
    {
        reg_psw |= 0x80;
    }
    else
    {
        reg_psw &= 0x7f;
    }
}

static void dex()
{
    reg_x--;

    if (reg_x)
    {
        reg_psw &= 0xfd;
    }
    else
    {
        reg_psw |= 0x02;
    }

    if (reg_x & 0x80)
    {
        reg_psw |= 0x80;
    }
    else
    {
        reg_psw &= 0x7f;
    }
}

static void dey()
{
    reg_y--;

    if (reg_y)
    {
        reg_psw &= 0xfd;
    }
    else
    {
        reg_psw |= 0x02;
    }

    if (reg_y & 0x80)
    {
        reg_psw |= 0x80;
    }
    else
    {
        reg_psw &= 0x7f;
    }
}

static void eor()
{
    instruction_table[opcode].address_mode();
    reg_a ^= system_read_memory(save_pc);

    if (reg_a)
    {
        reg_psw &= 0xfd;
    }
    else
    {
        reg_psw |= 0x02;
    }

    if (reg_a & 0x80)
    {
        reg_psw |= 0x80;
    }
    else
    {
        reg_psw &= 0x7f;
    }
}

static void inc()
{
    instruction_table[opcode].address_mode();
    system_write_memory(save_pc, byte_value = system_read_memory(save_pc) + 1);

    if (byte_value)
    {
        reg_psw &= 0xfd;
    }
    else
    {
        reg_psw |= 0x02;
    }

    if (byte_value & 0x80)
    {
        reg_psw |= 0x80;
    }
    else
    {
        reg_psw &= 0x7f;
    }
}

static void inx()
{
    reg_x++;

    if (reg_x)
    {
        reg_psw &= 0xfd;
    }
    else
    {
        reg_psw |= 0x02;
    }

    if (reg_x & 0x80)
    {
        reg_psw |= 0x80;
    }
    else
    {
        reg_psw &= 0x7f;
    }
}

static void iny()
{
    reg_y++;

    if (reg_y)
    {
        reg_psw &= 0xfd;
    }
    else
    {
        reg_psw |= 0x02;
    }

    if (reg_y & 0x80)
    {
        reg_psw |= 0x80;
    }
    else
    {
        reg_psw &= 0x7f;
    }
}

static void jmp()
{
    instruction_table[opcode].address_mode();
    reg_pc = save_pc;
}

static void jsr()
{
    reg_pc++;
    system_write_memory(0x0100 + reg_sp--, (uint8_t)(reg_pc >> 8));
    system_write_memory(0x0100 + reg_sp--, (uint8_t)(reg_pc & 0xff));
    reg_pc--;
    instruction_table[opcode].address_mode();
    reg_pc = save_pc;
}

static void lda()
{
    instruction_table[opcode].address_mode();
    reg_a = system_read_memory(save_pc);

    // set the zero flag
    if (reg_a)
    {
        reg_psw &= 0xfd;
    }
    else
    {
        reg_psw |= 0x02;
    }

    // set the negative flag
    if (reg_a & 0x80)
    {
        reg_psw |= 0x80;
    }
    else
    {
        reg_psw &= 0x7f;
    }
}

static void ldx()
{
    instruction_table[opcode].address_mode();
    reg_x = system_read_memory(save_pc);

    if (reg_x)
    {
        reg_psw &= 0xfd;
    }
    else
    {
        reg_psw |= 0x02;
    }

    if (reg_x & 0x80)
    {
        reg_psw |= 0x80;
    }
    else
    {
        reg_psw &= 0x7f;
    }
}

static void ldy()
{
    instruction_table[opcode].address_mode();
    reg_y = system_read_memory(save_pc);

    if (reg_y)
    {
        reg_psw &= 0xfd;
    }
    else
    {
        reg_psw |= 0x02;
    }

    if (reg_y & 0x80)
    {
        reg_psw |= 0x80;
    }
    else
    {
        reg_psw &= 0x7f;
    }
}

static void lsr()
{
    instruction_table[opcode].address_mode();
    byte_value = system_read_memory(save_pc);
    /* set carry flag if shifting right causes a bit to be lost */
    reg_psw = (reg_psw & 0xfe) | (byte_value & 0x01);
    byte_value = byte_value >> 1;
    system_write_memory(save_pc, byte_value);

    /* set zero flag if m_bValue is zero */
    if (byte_value != 0)
    {
        reg_psw &= 0xfd;
    }
    else
    {
        reg_psw |= 0x02;
    }

    /* set negative flag if bit 8 set??? can this happen on an LSR? */
    if ((byte_value & 0x80) == 0x80)
    {
        reg_psw |= 0x80;
    }
    else
    {
        reg_psw &= 0x7f;
    }
}

static void lsra()
{
    reg_psw = (reg_psw & 0xfe) | (reg_a & 0x01);
    reg_a = reg_a >> 1;

    if (reg_a)
    {
        reg_psw &= 0xfd;
    }
    else
    {
        reg_psw |= 0x02;
    }

    if (reg_a & 0x80)
    {
        reg_psw |= 0x80;
    }
    else
    {
        reg_psw &= 0x7f;
    }
}

static void nop()
{
}

static void ora()
{
    instruction_table[opcode].address_mode();
    reg_a |= system_read_memory(save_pc);

    if (reg_a)
    {
        reg_psw &= 0xfd;
    }
    else
    {
        reg_psw |= 0x02;
    }

    if (reg_a & 0x80)
    {
        reg_psw |= 0x80;
    }
    else
    {
        reg_psw &= 0x7f;
    }
}

static void pha()
{
    system_write_memory(0x100 + reg_sp--, reg_a);
}

static void php()
{
    system_write_memory(0x100 + reg_sp--, reg_psw);
}

static void pla()
{
    reg_a = system_read_memory(++reg_sp + 0x100);

    if (reg_a)
    {
        reg_psw &= 0xfd;
    }
    else
    {
        reg_psw |= 0x02;
    }

    if (reg_a & 0x80)
    {
        reg_psw |= 0x80;
    }
    else
    {
        reg_psw &= 0x7f;
    }
}

static void plp()
{
    reg_psw = system_read_memory(++reg_sp + 0x100) | 0x20;
}

static void rol()
{
    save_carry = (reg_psw & 0x01);
    instruction_table[opcode].address_mode();
    byte_value = system_read_memory(save_pc);
    reg_psw = (reg_psw & 0xfe) | ((byte_value >> 7) & 0x01);
    byte_value = byte_value << 1;
    byte_value |= save_carry;
    system_write_memory(save_pc, byte_value);

    if (byte_value)
    {
        reg_psw &= 0xfd;
    }
    else
    {
        reg_psw |= 0x02;
    }

    if (byte_value & 0x80)
    {
        reg_psw |= 0x80;
    }
    else
    {
        reg_psw &= 0x7f;
    }
}

static void rola()
{
    save_carry = (reg_psw & 0x01);
    reg_psw = (reg_psw & 0xfe) | ((reg_a >> 7) & 0x01);
    reg_a = reg_a << 1;
    reg_a |= save_carry;

    if (reg_a)
    {
        reg_psw &= 0xfd;
    }
    else
    {
        reg_psw |= 0x02;
    }

    if (reg_a & 0x80)
    {
        reg_psw |= 0x80;
    }
    else
    {
        reg_psw &= 0x7f;
    }
}

static void ror()
{
    save_carry = (reg_psw & 0x01);
    instruction_table[opcode].address_mode();
    byte_value = system_read_memory(save_pc);
    reg_psw = (reg_psw & 0xfe) | (byte_value & 0x01);
    byte_value = byte_value >> 1;

    if (save_carry)
    {
        byte_value |= 0x80;
    }

    system_write_memory(save_pc, byte_value);

    if (byte_value)
    {
        reg_psw &= 0xfd;
    }
    else
    {
        reg_psw |= 0x02;
    }

    if (byte_value & 0x80)
    {
        reg_psw |= 0x80;
    }
    else
    {
        reg_psw &= 0x7f;
    }
}

static void rora()
{
    save_carry = (reg_psw & 0x01);
    reg_psw = (reg_psw & 0xfe) | (reg_a & 0x01);
    reg_a = reg_a >> 1;

    if (save_carry)
    {
        reg_a |= 0x80;
    }

    if (reg_a)
    {
        reg_psw &= 0xfd;
    }
    else
    {
        reg_psw |= 0x02;
    }

    if (reg_a & 0x80)
    {
        reg_psw |= 0x80;
    }
    else
    {
        reg_psw &= 0x7f;
    }
}

static void rti()
{
    reg_psw = system_read_memory(++reg_sp + 0x100) | 0x20;
    reg_pc = system_read_memory(++reg_sp + 0x100);
    reg_pc |= (system_read_memory(++reg_sp + 0x100) << 8);
}

static void rts()
{
    reg_pc = system_read_memory(++reg_sp + 0x100);
    reg_pc |= (system_read_memory(++reg_sp + 0x100) << 8);
    reg_pc++;
}

static void sbc()
{
    instruction_table[opcode].address_mode();
    byte_value = system_read_memory(save_pc);
    save_carry = 1 - (reg_psw & 0x01);
    sum = ((int) reg_a) - ((int) byte_value) - save_carry;

    if ((sum > 0x7f) || (sum < -0x80))
    {
        reg_psw |= 0x40;
    }
    else
    {
        reg_psw &= 0xbf;
    }

    if (reg_psw & 0x08)
    {
        uint8_t bHalfCarry = 0x10;
        int operator_1 = reg_a;
        int store = (operator_1 & 0x0f) - ((int) byte_value & 0x0f) - save_carry;
        reg_a = ((store & 0x10) == 0) ? store : (store - 6);
        store = (operator_1 & 0xf0) - ((int)byte_value & 0xf0) - ((int)reg_a & 0x10);
        reg_a = ((int) reg_a & 0x0f) | (((store & 0x100) == 0) ? store : (store - 0x60));

        if ((store & 0x100) == 0)
        {
            reg_psw |= 0x01;
        }
        else
        {
            reg_psw &= 0xfe;
        }
    }
    else
    {
        if ((sum & 0x100) == 0)
        {
            reg_psw |= 0x01;
        }
        else
        {
            reg_psw &= 0xfe;
        }

        reg_a = sum & 0xff;
    }

    instruction_ticks++;

    if (reg_a)
    {
        reg_psw &= 0xfd;
    }
    else
    {
        reg_psw |= 0x02;
    }

    if (reg_a & 0x80)
    {
        reg_psw |= 0x80;
    }
    else
    {
        reg_psw &= 0x7f;
    }
}

static void sec()
{
    reg_psw |= 0x01;
}

static void sed()
{
    reg_psw |= 0x08;
}

static void sei()
{
    reg_psw |= 0x04;
}

static void sta()
{
    instruction_table[opcode].address_mode();
    system_write_memory(save_pc, reg_a);
}

static void stx()
{
    instruction_table[opcode].address_mode();
    system_write_memory(save_pc, reg_x);
}

static void sty()
{
    instruction_table[opcode].address_mode();
    system_write_memory(save_pc, reg_y);
}

static void tax()
{
    reg_x = reg_a;

    if (reg_x)
    {
        reg_psw &= 0xfd;
    }
    else
    {
        reg_psw |= 0x02;
    }

    if (reg_x & 0x80)
    {
        reg_psw |= 0x80;
    }
    else
    {
        reg_psw &= 0x7f;
    }
}

static void tay()
{
    reg_y = reg_a;

    if (reg_y)
    {
        reg_psw &= 0xfd;
    }
    else
    {
        reg_psw |= 0x02;
    }

    if (reg_y & 0x80)
    {
        reg_psw |= 0x80;
    }
    else
    {
        reg_psw &= 0x7f;
    }
}

static void tsx()
{
    reg_x = reg_sp;

    if (reg_x)
    {
        reg_psw &= 0xfd;
    }
    else
    {
        reg_psw |= 0x02;
    }

    if (reg_x & 0x80)
    {
        reg_psw |= 0x80;
    }
    else
    {
        reg_psw &= 0x7f;
    }
}

static void txa()
{
    reg_a = reg_x;

    if (reg_a)
    {
        reg_psw &= 0xfd;
    }
    else
    {
        reg_psw |= 0x02;
    }

    if (reg_a & 0x80)
    {
        reg_psw |= 0x80;
    }
    else
    {
        reg_psw &= 0x7f;
    }
}

static void txs()
{
    reg_sp = reg_x;
}

static void tya()
{
    reg_a = reg_y;

    if (reg_a)
    {
        reg_psw &= 0xfd;
    }
    else
    {
        reg_psw |= 0x02;
    }

    if (reg_a & 0x80)
    {
        reg_psw |= 0x80;
    }
    else
    {
        reg_psw &= 0x7f;
    }
}

static void bra()
{
    instruction_table[opcode].address_mode();
    reg_pc += save_pc;
    instruction_ticks++;
}

static void dea()
{
    reg_a--;

    if (reg_a)
    {
        reg_psw &= 0xfd;
    }
    else
    {
        reg_psw |= 0x02;
    }

    if (reg_a & 0x80)
    {
        reg_psw |= 0x80;
    }
    else
    {
        reg_psw &= 0x7f;
    }
}

static void ina()
{
    reg_a++;

    if (reg_a)
    {
        reg_psw &= 0xfd;
    }
    else
    {
        reg_psw |= 0x02;
    }

    if (reg_a & 0x80)
    {
        reg_psw |= 0x80;
    }
    else
    {
        reg_psw &= 0x7f;
    }
}

static void phx()
{
    system_write_memory(0x100 + reg_sp--, reg_x);
}

static void plx()
{
    reg_x = system_read_memory(++reg_sp + 0x100);

    if (reg_x)
    {
        reg_psw &= 0xfd;
    }
    else
    {
        reg_psw |= 0x02;
    }

    if (reg_x & 0x80)
    {
        reg_psw |= 0x80;
    }
    else
    {
        reg_psw &= 0x7f;
    }
}

static void phy()
{
    system_write_memory(0x100 + reg_sp--, reg_y);
}

static void ply()
{
    reg_y = system_read_memory(++reg_sp + 0x100);

    if (reg_y)
    {
        reg_psw &= 0xfd;
    }
    else
    {
        reg_psw |= 0x02;
    }

    if (reg_y & 0x80)
    {
        reg_psw |= 0x80;
    }
    else
    {
        reg_psw &= 0x7f;
    }
}

static void stz()
{
    instruction_table[opcode].address_mode();
    system_write_memory(save_pc, 0);
}

static void tsb()
{
    instruction_table[opcode].address_mode();
    system_write_memory(save_pc, system_read_memory(save_pc) | reg_a);

    if (system_read_memory(save_pc))
    {
        reg_psw &= 0xfd;
    }
    else
    {
        reg_psw |= 0x02;
    }
}

static void trb()
{
    instruction_table[opcode].address_mode();
    system_write_memory(save_pc, system_read_memory(save_pc) & (reg_a ^ 0xff));

    if (system_read_memory(save_pc))
    {
        reg_psw &= 0xfd;
    }
    else
    {
        reg_psw |= 0x02;
    }
}


static const instruction_t instruction_table[256] =
{
    { 7, brk, implied },                 // 0x00
    { 6, ora, indirect_x },              // 0x01
    { 2, brk, implied },                 // 0x02
    { 2, brk, implied },                 // 0x03
    { 3, tsb, zero_page },               // 0x04
    { 3, ora, zero_page },               // 0x05
    { 5, asl, zero_page },               // 0x06
    { 2, brk, implied },                 // 0x07
    { 3, php, implied },                 // 0x08
    { 3, ora, immediate },               // 0x09
    { 2, asla, implied },                // 0x0A
    { 2, brk, implied },                 // 0x0B
    { 4, tsb, absolute },                // 0x0C
    { 4, ora, absolute },                // 0x0D
    { 6, asl, absolute },                // 0x0E
    { 2, brk, implied },                 // 0x0F
    { 2, bpl, relative },                // 0x10
    { 5, ora, indirect_y },              // 0x11
    { 3, ora, indirect_zero_page },      // 0x12
    { 2, brk, implied },                 // 0x13
    { 3, trb, zero_page },               // 0x14
    { 4, ora, zero_page_x },             // 0x15
    { 6, asl, zero_page_x },             // 0x16
    { 2, brk, implied },                 // 0x17
    { 2, clc, implied },                 // 0x18
    { 4, ora, absolute_y },              // 0x19
    { 2, ina, implied },                 // 0x1A
    { 2, brk, implied },                 // 0x1B
    { 4, trb, absolute },                // 0x1C
    { 4, ora, absolute_x },              // 0x1D
    { 7, asl, absolute_x },              // 0x1E
    { 2, brk, implied },                 // 0x1F
    { 6, jsr, absolute },                // 0x20
    { 6, and, indirect_x },              // 0x21
    { 2, brk, implied },                 // 0x22
    { 2, brk, implied },                 // 0x23
    { 3, bit, zero_page },               // 0x24
    { 3, and, zero_page },               // 0x25
    { 5, rol, zero_page },               // 0x26
    { 2, brk, implied },                 // 0x27
    { 4, plp, implied },                 // 0x28
    { 3, and, immediate },               // 0x29
    { 2, rola, implied },                // 0x2A
    { 2, brk, implied },                 // 0x2B
    { 4, bit, absolute },                // 0x2C
    { 4, and, absolute },                // 0x2D
    { 6, rol, absolute },                // 0x2E
    { 2, brk, implied },                 // 0x2F
    { 2, bmi, relative },                // 0x30
    { 5, and, indirect_y },              // 0x31
    { 3, and, indirect_zero_page },      // 0x32
    { 2, brk, implied },                 // 0x33
    { 4, bit, zero_page_x },             // 0x34
    { 4, and, zero_page_x },             // 0x35
    { 6, rol, zero_page_x },             // 0x36
    { 2, brk, implied },                 // 0x37
    { 2, sec, implied },                 // 0x38
    { 4, and, absolute_y },              // 0x39
    { 2, dea, implied },                 // 0x3A
    { 2, brk, implied },                 // 0x3B
    { 4, bit, absolute_x },              // 0x3C
    { 4, and, absolute_x },              // 0x3D
    { 7, rol, absolute_x },              // 0x3E
    { 2, brk, implied },                 // 0x3F
    { 6, rti, implied },                 // 0x40
    { 6, eor, indirect_x },              // 0x41
    { 2, brk, implied },                 // 0x42
    { 2, brk, implied },                 // 0x43
    { 2, brk, implied },                 // 0x44
    { 3, eor, zero_page },               // 0x45
    { 5, lsr, zero_page },               // 0x46
    { 2, brk, implied },                 // 0x47
    { 3, pha, implied },                 // 0x48
    { 3, eor, immediate },               // 0x49
    { 2, lsra, implied },                // 0x4A
    { 2, brk, implied },                 // 0x4B
    { 3, jmp, absolute },                // 0x4C
    { 4, eor, absolute },                // 0x4D
    { 6, lsr, absolute },                // 0x4E
    { 2, brk, implied },                 // 0x4F
    { 2, bvc, relative },                // 0x50
    { 5, eor, indirect_y },              // 0x51
    { 3, eor, indirect_zero_page },      // 0x52
    { 2, brk, implied },                 // 0x53
    { 2, brk, implied },                 // 0x54
    { 4, eor, zero_page_x },             // 0x55
    { 6, lsr, zero_page_x },             // 0x56
    { 2, brk, implied },                 // 0x57
    { 2, cli, implied },                 // 0x58
    { 4, eor, absolute_y },              // 0x59
    { 3, phy, implied },                 // 0x5A
    { 2, brk, implied },                 // 0x5B
    { 2, brk, implied },                 // 0x5C
    { 4, eor, absolute_x },              // 0x5D
    { 7, lsr, absolute_x },              // 0x5E
    { 2, brk, implied },                 // 0x5F
    { 6, rts, implied },                 // 0x60
    { 6, adc, indirect_x },              // 0x61
    { 2, brk, implied },                 // 0x62
    { 2, brk, implied },                 // 0x63
    { 3, stz, zero_page },               // 0x64
    { 3, adc, zero_page },               // 0x65
    { 5, ror, zero_page },               // 0x66
    { 2, brk, implied },                 // 0x67
    { 4, pla, implied },                 // 0x68
    { 3, adc, immediate },               // 0x69
    { 2, rora, implied },                // 0x6A
    { 2, brk, implied },                 // 0x6B
    { 5, jmp, indirect },                // 0x6C
    { 4, adc, absolute },                // 0x6D
    { 6, ror, absolute },                // 0x6E
    { 2, brk, implied },                 // 0x6F
    { 2, bvs, relative },                // 0x70
    { 5, adc, indirect_y },              // 0x71
    { 3, adc, indirect_zero_page },      // 0x72
    { 2, brk, implied },                 // 0x73
    { 4, stz, zero_page_x },             // 0x74
    { 4, adc, zero_page_x },             // 0x75
    { 6, ror, zero_page_x },             // 0x76
    { 2, brk, implied },                 // 0x77
    { 2, sei, implied },                 // 0x78
    { 4, adc, absolute_y },              // 0x79
    { 4, ply, implied },                 // 0x7A
    { 2, brk, implied },                 // 0x7B
    { 6, jmp, indirect_absolute_x },     // 0x7C
    { 4, adc, absolute_x },              // 0x7D
    { 7, ror, absolute_x },              // 0x7E
    { 2, brk, implied },                 // 0x7F
    { 2, bra, relative },                // 0x80
    { 6, sta, indirect_x },              // 0x81
    { 2, brk, implied },                 // 0x82
    { 2, brk, implied },                 // 0x83
    { 2, sty, zero_page },               // 0x84
    { 2, sta, zero_page },               // 0x85
    { 2, stx, zero_page },               // 0x86
    { 2, brk, implied },                 // 0x87
    { 2, dey, implied },                 // 0x88
    { 2, bit, immediate },               // 0x89
    { 2, txa, implied },                 // 0x8A
    { 2, brk, implied },                 // 0x8B
    { 4, sty, absolute },                // 0x8C
    { 4, sta, absolute },                // 0x8D
    { 4, stx, absolute },                // 0x8E
    { 2, brk, implied },                 // 0x8F
    { 2, bcc, relative },                // 0x90
    { 6, sta, indirect_y },              // 0x91
    { 3, sta, indirect_zero_page },      // 0x92
    { 2, brk, implied },                 // 0x93
    { 4, sty, zero_page_x },             // 0x94
    { 4, sta, zero_page_x },             // 0x95
    { 4, stx, zero_page_y },             // 0x96
    { 2, brk, implied },                 // 0x97
    { 2, tya, implied },                 // 0x98
    { 5, sta, absolute_y },              // 0x99
    { 2, txs, implied },                 // 0x9A
    { 2, brk, implied },                 // 0x9B
    { 4, stz, absolute },                // 0x9C
    { 5, sta, absolute_x },              // 0x9D
    { 5, stz, absolute_x },              // 0x9E
    { 2, brk, implied },                 // 0x9F
    { 3, ldy, immediate },               // 0xA0
    { 6, lda, indirect_x },              // 0xA1
    { 3, ldx, immediate },               // 0xA2
    { 2, brk, implied },                 // 0xA3
    { 3, ldy, zero_page },               // 0xA4
    { 3, lda, zero_page },               // 0xA5
    { 3, ldx, zero_page },               // 0xA6
    { 2, brk, implied },                 // 0xA7
    { 2, tay, implied },                 // 0xA8
    { 3, lda, immediate },               // 0xA9
    { 2, tax, implied },                 // 0xAA
    { 2, brk, implied },                 // 0xAB
    { 4, ldy, absolute },                // 0xAC
    { 4, lda, absolute },                // 0xAD
    { 4, ldx, absolute },                // 0xAE
    { 2, brk, implied },                 // 0xAF
    { 2, bcs, relative },                // 0xB0
    { 5, lda, indirect_y },              // 0xB1
    { 3, lda, indirect_zero_page },      // 0xB2
    { 2, brk, implied },                 // 0xB3
    { 4, ldy, zero_page_x },             // 0xB4
    { 4, lda, zero_page_x },             // 0xB5
    { 4, ldx, zero_page_y },             // 0xB6
    { 2, brk, implied },                 // 0xB7
    { 2, clv, implied },                 // 0xB8
    { 4, lda, absolute_y },              // 0xB9
    { 2, tsx, implied },                 // 0xBA
    { 2, brk, implied },                 // 0xBB
    { 4, ldy, absolute_x },              // 0xBC
    { 4, lda, absolute_x },              // 0xBD
    { 4, ldx, absolute_y },              // 0xBE
    { 2, brk, implied },                 // 0xBF
    { 3, cpy, immediate },               // 0xC0
    { 6, cmp, indirect_x },              // 0xC1
    { 2, brk, implied },                 // 0xC2
    { 2, brk, implied },                 // 0xC3
    { 3, cpy, zero_page },               // 0xC4
    { 3, cmp, zero_page },               // 0xC5
    { 5, dec, zero_page },               // 0xC6
    { 2, brk, implied },                 // 0xC7
    { 2, iny, implied },                 // 0xC8
    { 3, cmp, immediate },               // 0xC9
    { 2, dex, implied },                 // 0xCA
    { 2, brk, implied },                 // 0xCB
    { 4, cpy, absolute },                // 0xCC
    { 4, cmp, absolute },                // 0xCD
    { 6, dec, absolute },                // 0xCE
    { 2, brk, implied },                 // 0xCF
    { 2, bne, relative },                // 0xD0
    { 5, cmp, indirect_y },              // 0xD1
    { 3, cmp, indirect_zero_page },      // 0xD2
    { 2, brk, implied },                 // 0xD3
    { 2, brk, implied },                 // 0xD4
    { 4, cmp, zero_page_x },             // 0xD5
    { 6, dec, zero_page_x },             // 0xD6
    { 2, brk, implied },                 // 0xD7
    { 2, cld, implied },                 // 0xD8
    { 4, cmp, absolute_y },              // 0xD9
    { 3, phx, implied },                 // 0xDA
    { 2, brk, implied },                 // 0xDB
    { 2, brk, implied },                 // 0xDC
    { 4, cmp, absolute_x },              // 0xDD
    { 7, dec, absolute_x },              // 0xDE
    { 2, brk, implied },                 // 0xDF
    { 3, cpx, immediate },               // 0xE0
    { 6, sbc, indirect_x },              // 0xE1
    { 2, brk, implied },                 // 0xE2
    { 2, brk, implied },                 // 0xE3
    { 3, cpx, zero_page },               // 0xE4
    { 3, sbc, zero_page },               // 0xE5
    { 5, inc, zero_page },               // 0xE6
    { 2, brk, implied },                 // 0xE7
    { 2, inx, implied },                 // 0xE8
    { 3, sbc, immediate },               // 0xE9
    { 2, nop, implied },                 // 0xEA
    { 2, brk, implied },                 // 0xEB
    { 4, cpx, absolute },                // 0xEC
    { 4, sbc, absolute },                // 0xED
    { 6, inc, absolute },                // 0xEE
    { 2, brk, implied },                 // 0xEF
    { 2, beq, relative },                // 0xF0
    { 5, sbc, indirect_y },              // 0xF1
    { 3, sbc, indirect_zero_page },      // 0xF2
    { 2, brk, implied },                 // 0xF3
    { 2, brk, implied },                 // 0xF4
    { 4, sbc, zero_page_x },             // 0xF5
    { 6, inc, zero_page_x },             // 0xF6
    { 2, brk, implied },                 // 0xF7
    { 2, sed, implied },                 // 0xF8
    { 4, sbc, absolute_y },              // 0xF9
    { 4, plx, implied },                 // 0xFA
    { 2, brk, implied },                 // 0xFB
    { 2, brk, implied },                 // 0xFC
    { 4, sbc, absolute_x },              // 0xFD
    { 7, inc, absolute_x },              // 0xFE
    { 2, brk, implied },                 // 0xFF
};


/* Non maskable interrupt */
void cpu_6502_assert_nmi()
{
    flag_nmi = true;
    reg_psw &= ~PSW_B;
}

void nmi()
{
    system_write_memory(0x0100 + reg_sp--, (uint8_t)(reg_pc >> 8));
    system_write_memory(0x0100 + reg_sp--, (uint8_t)(reg_pc & 0xff));
    system_write_memory(0x0100 + reg_sp--, reg_psw);
    reg_psw |= 0x04;
    reg_pc = system_read_memory(0xfffa);
    reg_pc |= (uint16_t)system_read_memory(0xfffb) << 8;
    flag_nmi = false;
}

/* Maskable Interrupt */
void cpu_6502_assert_irq()
{
    flag_irq = true;
    reg_psw &= ~PSW_B;
}

void irq()
{
    system_write_memory(0x0100 + reg_sp--, (uint8_t)(reg_pc >> 8));
    system_write_memory(0x0100 + reg_sp--, (uint8_t)(reg_pc & 0xff));
    system_write_memory(0x0100 + reg_sp--, reg_psw);
    reg_psw |= 0x04;
    reg_pc = system_read_memory(0xfffe);
    reg_pc |= (uint16_t)system_read_memory(0xffff) << 8;
    flag_irq = false;
}

/* Execute a number of instructions */
void cpu_6502_execute(int timer_ticks)
{
    uint32_t cpu_ticks;

    while (timer_ticks > 0)
    {
        opcode = system_read_memory(reg_pc++);
        instruction_ticks = instruction_table[opcode].ticks;
        instruction_table[opcode].instruction();
        cpu_ticks = instruction_ticks;
        timer_ticks -= cpu_ticks;

        if (via_6522_update(instruction_ticks))
        {
            flag_irq = true;
        }

        if (delayed_nmi_counter > 0)
        {
            delayed_nmi_counter -= instruction_length;

            if (delayed_nmi_counter <= 0)
            {
                flag_nmi = true;
            }
        }

        if ((flag_irq) && ((reg_psw & PSW_I) == 0))
        {
            irq();
        }

        if (flag_nmi)
        {
            nmi();
        }
    }
}


void cpu_6502_delayed_nmi_callback(uint16_t address, uint8_t value)
{
    if ((address & 0x03) == 1)
    {
        delayed_nmi_counter = 8;
    }
}


void cpu_6502_continue(uint16_t pc, uint8_t a, uint8_t ix, uint8_t iy, uint8_t sp, uint8_t psw)
{
    reg_pc = pc;
    reg_a = a;
    reg_x = ix;
    reg_y = iy;
    reg_sp = sp;
    reg_psw = psw;
    flag_irq = false;
    flag_nmi = false;
}


void cpu_6502_reset(uint8_t bank, uint16_t address)
{
    reg_a = 0;
    reg_x = 0;
    reg_y = 0;
    reg_psw = 0x20;
    reg_sp = 0xff;
    reg_pc = system_read_memory(0xfffc);
    reg_pc |= (uint16_t)system_read_memory(0xfffd) << 8;
    flag_irq = false;
    flag_nmi = false;
//    system_write_memory(0xbc04, 0xff);
    /*
        PlaySound(NULL, AfxGetApp()->m_hInstance, SND_PURGE);
        AYResetChip(0);
        AYResetChip(1);
    */
}

int cpu_6502_initialise(uint8_t bank, uint16_t address, uint16_t param, char *identifier)
{
    system_register_memory_mapped_device(0xBFF0, 0xBFFF, NULL, cpu_6502_delayed_nmi_callback, false);
    cpu_6502_reset(bank, address);
    return RV_OK;
}


