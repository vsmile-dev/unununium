// Copyright 2008  Segher Boessenkool  <segher@kernel.crashing.org>
// Licensed under the terms of the GNU GPL, version 2
// http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

#include "types.h"
#include "disas.h"
#include "platform.h"
#include "emu.h"


#define FREQ 60
#define PERIOD (500000/FREQ)


u16 all_the_mem[4*N_MEM];
u16 mem[N_MEM];

static int trainer = 0;

static int trace = 0;
static int trace_new = 0;
static int store_trace = 0;
static int pause_after_every_frame = 0;
static u8 ever_ran_this[N_MEM];

static u16 reg[8];
static u8 sb;
static u8 irq, fiq;

static u64 insn_count;

static u32 idle_pc;


static void switch_bank(u32 bank)
{
	memcpy(mem + 0x4000, all_the_mem + (bank << 22) + 0x4000, 2*(N_MEM - 0x4000));
	memset(ever_ran_this, 0, N_MEM);

	idle_pc = 0;
	if (mem[0x19792] == 0x4311 && mem[0x19794] == 0x4e43)	// VII bank 0
		idle_pc = 0x19792;
	if (mem[0x21653] == 0x4311 && mem[0x21655] == 0x4e43)	// VII bank 2
		idle_pc = 0x21653;
	if (mem[0x42daa] == 0x4311 && mem[0x42dac] == 0x4e43) {	// VC1
		idle_pc = 0x42daa;
		controller_should_be_rotated = 1;
		if (trainer)
			mem[0x38ecf] = 0;	// no life lost in LP
	}
}

static void dump(u32 addr, u32 len)
{
	u32 off, i;

	for (off = addr & ~0x0f; off < addr + len; off += 0x10) {
		printf("%06x:", off);

		for (i = 0; i < 16; i++)
			printf(" %04x", mem[off+i]);
		printf("\n");
	}
}

static void video_store(u16 val, u32 addr)
{
	if (addr < 0x2900) {		// video regs
		switch (addr) {
		case 0x2810 ... 0x2815:	// page 0 regs
		case 0x2816 ... 0x281b:	// page 1 regs
			break;

		case 0x281c:		// XXX
			if (val != 0x0020)
				printf("VIDEO STORE %04x to %04x\n", val, addr);
			break;

		case 0x2820 ... 0x2822:	// bitmap offsets
			break;

		case 0x2836:
		case 0x2837:		// XXX
			if (val != 0xffff)
				printf("VIDEO STORE %04x to %04x\n", val, addr);
			break;

		case 0x2842:		// XXX
			if (val != 0x0001)
				printf("VIDEO STORE %04x to %04x\n", val, addr);
			break;

		case 0x2862:		// video IRQ enable
			break;

		case 0x2863:		// video IRQ ACK
			mem[addr] &= ~val;
			if (val & 1)
				update_screen();
			return;

		default:
			printf("VIDEO STORE %04x to %04x\n", val, addr);
		}
	} else if (addr < 0x2b00) {	// scroll per raster line
	} else if (addr < 0x2c00) {	// palette
	} else {			// sprites
	}

	mem[addr] = val;
}

static void audio_store(u16 val, u32 addr)
{
	mem[addr] = val;

	if (addr < 0x3200) {		// XXX
		return;
	} else if (addr < 0x3400) {	// XXX
		return;
	} else {			// XXX
		return;
	}

	printf("AUDIO STORE %04x to %04x\n", val, addr);
}

static void io_store(u16 val, u32 addr)
{
	switch (addr) {
	case 0x3d07:		// port B data write
		printf("STORE %04x to %04x (port B)\n", val, addr);
		u32 bank = ((val & 0x80) >> 7) | ((val & 0x20) >> 4);
		switch_bank(bank);
		printf("switched to bank %x\n", bank);
		break;

	case 0x3d01 ... 0x3d06:
	case 0x3d08 ... 0x3d0f:	// ports A..C
		break;

	case 0x3d22:		// IRQ ack
		mem[addr] &= ~val;
		return;

	case 0x3d24:		// XXX
		if (val != 0x55aa)
			printf("IO STORE %04x to %04x\n", val, addr);
		break;

	case 0x3d2f:		// set DS
		reg[6] = (reg[6] & 0x03ff) | (val << 10);
		break;

	case 0x3d31:		// XXX UART
		if (val != 0x0003)
			printf("IO STORE %04x to %04x\n", val, addr);
		break;

	case 0x3d33:		// UART baud rate
		printf("SET UART BAUD RATE to %d\n", 27000000 / (0x10000 - val));
		break;

	case 0x3d35:		// UART TX data
		break;

	default:
		printf("IO STORE %04x to %04x\n", val, addr);
	}

	mem[addr] = val;
}

static void store(u16 val, u32 addr)
{
	if (store_trace)
		printf("WRITE %04x TO %04x (was %04x)\n", val, addr, mem[addr]);

	if (addr < 0x2800) {	// RAM
		mem[addr] = val;
		return;
	}

	if (addr < 0x3000) {
		video_store(val, addr);
		return;
	}

	if (addr < 0x3500) {
		audio_store(val, addr);
		return;
	}

	if (addr >= 0x3d00 && addr < 0x3e00) {
		io_store(val, addr);
		return;
	}

	if (addr < 0x4000) {
		printf("BAD STORE %04x to %04x\n", val, addr);
		return;
	}

	printf("ROM STORE %04x to %04x\n", val, addr);
}

static u16 io_load(u32 addr)
{
	u16 val = mem[addr];

	if (addr >= 0x2800 && addr < 0x2900) {		// video regs
//		if (addr == 0x2863) {
//			u16 val = 1 << (random() % 3);
//			printf("(random 1, 2, 4) LOAD %04x from %04x\n", val, addr);
//			return val;
//		}
		//printf("LOAD %04x from %04x\n", val, addr);
		return val;
	}
	if (addr >= 0x2b00 && addr < 0x2c00) {		// palette
		//printf("LOAD %04x from %04x\n", val, addr);
		return val;
	}
	if (addr >= 0x2c00 && addr < 0x3000) {		// sprite regs
		//printf("LOAD %04x from %04x\n", val, addr);
		return val;
	}
	if (addr >= 0x3000 && addr < 0x3200) {		// audio something
		//printf("LOAD %04x from %04x\n", val, addr);
		return val;
	}
	if (addr >= 0x3200 && addr < 0x3300) {		// audio something
		//printf("LOAD %04x from %04x\n", val, addr);
		return val;
	}
	if (addr >= 0x3400 && addr < 0x3600) {		// audio something
		//printf("LOAD %04x from %04x\n", val, addr);
		return val;
	}
	if (addr >= 0x3d00 && addr < 0x3e00) {		// I/O
		if (addr >= 0x3d01 && addr <= 0x3d0f)	// GPIO
			return val;

		if (addr == 0x3d22)	// IRQ
			return mem[0x3d21];

		if (addr == 0x3d2c || addr == 0x3d2d)
			return random();

		if (addr == 0x3d31) {
			val = 3;
			//printf("(hard 3) LOAD %04x from %04x\n", val, addr);
			return val;
		}

		if (addr == 0x3d36) {
			static int count = 0;
			val = controller_input[count];
			count = (count + 1) % 8;
			return val;
		}

		printf("LOAD %04x from %04x\n", val, addr);
		return val;
	}

	printf("UNKNOWN LOAD %04x from %04x\n", val, addr);
	return mem[addr];
}

static inline u16 load(u32 addr)
{
	u16 val = mem[addr];

	if (addr < 0x2800)	// RAM
		return val;

	if (addr >= 0x4000)	// ROM
		return val;

	return io_load(addr);
}

static inline u32 cs_pc(void)
{
	return ((reg[6] & 0x3f) << 16) | reg[7];
}

static void push(u16 val, u8 b)
{
	store(val, reg[b]--);
}

static u16 pop(u8 b)
{
	return load(++reg[b]);
}

static void print_state(void)
{
	int i;

	printf("\n[insn_count = %llu]\n", insn_count);
	printf(" SP   R1   R2   R3   R4   BP   SR   PC   CS NZSC DS  SB IRQ FIQ\n");
	for (i = 0; i < 8; i++)
		printf("%04x ", reg[i]);
	printf(" %02x", reg[6] & 0x3f);
	printf(" %x%x%x%x", (reg[6] >> 9) & 1, (reg[6] >> 8) & 1,
	                    (reg[6] >> 7) & 1, (reg[6] >> 6) & 1);
	printf(" %02x", reg[6] >> 10);
	printf("  %x   %x   %x\n", sb, irq, fiq);
	disas(mem, cs_pc());
}

static void step(void)
{
	u16 op;
	u8 op0, opA, op1, opN, opB, opimm;
	u16 x0, x1;
	u32 old_cs_pc = cs_pc();
	u32 x, d = 0xff0000;

	op = mem[cs_pc()];
	reg[7]++;


	// the top four bits are the alu op or the branch condition, or E or F
	op0 = (op >> 12);

	// the next three are usually the destination register
	opA = (op >> 9) & 7;

	// and the next three the addressing mode
	op1 = (op >> 6) & 7;

	// the next three can be anything
	opN = (op >> 3) & 7;

	// and the last three usually the second register (source register)
	opB = op & 7;

	// the last six sometimes are a single immediate number
	opimm = op & 63;


	// jumps
	if (opA == 7 && op1 < 2) {
		switch (op0) {
		case 0:		// JB
			if ((reg[6] & 0x40) == 0)
				goto do_jump;
			break;
		case 1:		// JAE
			if ((reg[6] & 0x40) == 0x40)
				goto do_jump;
			break;
		case 2:		// JGE
			if ((reg[6] & 0x80) == 0)
				goto do_jump;
			break;
		case 3:		// JL
			if ((reg[6] & 0x80) == 0x80)
				goto do_jump;
			break;
		case 4:		// JNE
			if ((reg[6] & 0x0100) == 0)
				goto do_jump;
			break;
		case 5:		// JE
			if ((reg[6] & 0x0100) == 0x0100)
				goto do_jump;
			break;
		case 6:		// JPL
			if ((reg[6] & 0x0200) == 0)
				goto do_jump;
			break;
		case 7:		// JMI
			if ((reg[6] & 0x0200) == 0x0200)
				goto do_jump;
			break;
		case 8:		// JBE
			if ((reg[6] & 0x0140) != 0x0040)
				goto do_jump;
			break;
		case 9:		// JA
			if ((reg[6] & 0x0140) == 0x0040)
				goto do_jump;
			break;
		case 10:	// JLE
			if ((reg[6] & 0x0180) != 0)
				goto do_jump;
			break;
		case 11:	// JG
			if ((reg[6] & 0x0180) == 0)
				goto do_jump;
			break;
		case 14:	// JMP
			goto do_jump;
		default:
			goto bad;

		do_jump:
			if (op1 == 0)
				reg[7] += opimm;
			else
				reg[7] -= opimm;
		}
		return;
	}


	// PUSH
	if (op1 == 2 && op0 == 13) {
		while (opN--)
			push(reg[opA--], opB);
		return;
	}

	// POP
	if (op1 == 2 && op0 == 9) {
		// special case for RETI
		if (opA == 5 && opN == 3 && opB == 0) {
			reg[6] = pop(0);
			reg[7] = pop(0);
			if (fiq & 2)
				fiq &= 1;
			else if (irq & 2)
				irq &= 1;
			return;
		}
		while (opN--)
			reg[++opA] = pop(opB);
		return;
	}


	if (op0 == 15) {
		switch (op1) {
		case 0:		// MUL US
			if (opN == 1) {
				if (opA == 7)
					goto bad;
				x = reg[opA]*reg[opB];
				if (reg[opB] & 0x8000)
					x -= reg[opA] << 16;
				reg[4] = x >> 16;
				reg[3] = x;
				return;
			} else
				goto bad;
		case 1:		// CALL
			x1 = mem[cs_pc()];
			reg[7]++;
			push(reg[7], 0);
			push(reg[6], 0);
			reg[7] = x1;
			reg[6] = (reg[6] & 0xffc0) | opimm;
			return;
		case 4:		// MUL SS
			if (opN == 1) {
				if (opA == 7)
					goto bad;
				x = reg[opA]*reg[opB];
				if (reg[opB] & 0x8000)
					x -= reg[opA] << 16;
				if (reg[opA] & 0x8000)
					x -= reg[opB] << 16;
				reg[4] = x >> 16;
				reg[3] = x;
				return;
			} else
				goto bad;
		case 5:		// IRQ/FIQ ON/OFF
			switch (opimm) {
			case 8:
				irq &= ~1;
				printf("IRQ OFF\n");
				return;
			case 9:
				irq |= 1;
				printf("IRQ ON\n");
				return;
			case 12:
				fiq &= ~1;
				printf("FIQ OFF\n");
				return;
			case 14:
				fiq |= 1;
				printf("FIQ ON\n");
				return;
			default:
				goto bad;
			}
		default:
			goto bad;
		}
	}


	// alu op

	// first, get the arguments
	x0 = reg[opA];

	switch (op1) {
	case 0:		// [BP+imm6]
		d = reg[5] + opimm;
		if (op0 == 13)
			x1 = 0x0bad;
		else
			x1 = load(d);
		break;
	case 1:		// imm6
		x1 = opimm;
		break;
	case 3:		// [Rb] and friends
		if ((opN & 3) == 3)
			reg[opB]++;
		d = reg[opB];
		if (opN & 4)
			d |= (reg[6] << 6) & 0x3f0000;
		if (op0 == 13)
			x1 = 0x0bad;
		else
			x1 = load(d);
		if ((opN & 3) == 1)
			reg[opB]--;
		if ((opN & 3) == 2)
			reg[opB]++;
		break;
	case 4:
		switch(opN) {
		case 0:		// Rb
			x1 = reg[opB];
			break;
		case 1:		// imm16
			x0 = reg[opB];
			x1 = mem[cs_pc()];
			reg[7]++;
			break;
		case 2:		// [imm16]
			x0 = reg[opB];
			d = mem[cs_pc()];
			if (op0 == 13)
				x1 = 0x0bad;
			else
				x1 = load(d);
			reg[7]++;
			break;
		case 3:		// [imm16] = ...
			x0 = reg[opB];
			x1 = reg[opA];
			d = mem[cs_pc()];
			reg[7]++;
			break;
		default:	// ASR
			{
				u32 shift = (reg[opB] << 4) | sb;
				if (shift & 0x80000)
					shift |= 0xf00000;
				shift >>= (opN - 3);
				sb = shift & 0x0f;
				x1 = (shift >> 4) & 0xffff;
			}
		}
		break;
	case 5:
		if (opN < 4) {	// LSL
			u32 shift = ((sb << 16) | reg[opB]) << (opN + 1);
			sb = (shift >> 16) & 0xf;
			x1 = shift & 0xffff;
		} else {	// LSR
			u32 shift = ((reg[opB] << 4) | sb) >> (opN - 3);
			sb = shift & 0x0f;
			x1 = (shift >> 4) & 0xffff;
		}
		break;
	case 6:
		if (opN < 4) {	// ROL
			u32 shift = ((((sb << 16) | reg[opB]) << 4) | sb) << (opN + 1);
			sb = (shift >> 20) & 0x0f;
			x1 = (shift >> 4) & 0xffff;
		} else {	// ROR
			u32 shift = ((((sb << 16) | reg[opB]) << 4) | sb) >> (opN - 3);
			sb = shift & 0x0f;
			x1 = (shift >> 4) & 0xffff;
		}
		break;
	//case 7:
	//	x1 = load(opimm);
	//	break;
	default:
		goto bad;
	}

//printf("--> args: %04x %04x\n", x0, x1);

	// then, perform the alu op
	switch (op0) {
	case 0:		// ADD
		x = x0 + x1;
		break;
	case 1:		// ADC
		x = x0 + x1;
		if (reg[6] & 0x40)
			x++;
		break;
	case 2: case 4:	// SUB, CMP
		x = x0 + (~x1 & 0xffff) + 1;
		break;
	case 3:		// SBC
		x = x0 + (~x1 & 0xffff);
		if (reg[6] & 0x40)
			x++;
		break;
	case 6:		// NEG
		x = -x1;
		break;
	case 8:		// XOR
		x = x0 ^ x1;
		break;
	case 9:		// LOAD
		x = x1;
		break;
	case 10:	// OR
		x = x0 | x1;
		break;
	case 11: case 12: // AND, TEST
		x = x0 & x1;
		break;
	case 13:	// STORE
		store(x0, d);
		return;
	default:
		goto bad;
	}

	// set N and Z flags
	if (op0 < 13) {		// not STORE
		reg[6] = (reg[6] & ~0x0300);

		if (x & 0x8000)
			reg[6] |= 0x0200;

		if ((x & 0xffff) == 0)
			reg[6] |= 0x0100;
	}

	// set S and C flags
	if (op0 < 5) {		// ADD, ADC, SUB, SBC, CMP
		reg[6] = (reg[6] & ~0x0c0);

		if (x != (u16)x)
			reg[6] |= 0x40;

		// this only works for cmp, not for add (or sbc).
		if ((s16)x0 < (s16)x1)
			reg[6] |= 0x80;
	}

	if (op0 == 4 || op0 == 12)	// CMP, TEST
		return;

//printf("--> result: %04x\n", x);

	if (op1 == 4 && opN == 3)	// [imm16] = ...
		store(x, d);
	else
		reg[opA] = x;

	return;


bad:
	reg[7] = old_cs_pc;
	print_state();
	printf("! UNIMPLEMENTED\n");

	exit(1);
}

static u32 last_retrace_time = 0;

static void do_idle(void)
{
	u32 now;
	struct timeval tv;

//	printf("### IDLE ###\n");

	gettimeofday(&tv, 0);
	now = 1000000*tv.tv_sec + tv.tv_usec;
	if (now < last_retrace_time + PERIOD) {
//		printf("  sleeping %dus\n", last_retrace_time + PERIOD - now);
		usleep(last_retrace_time + PERIOD - now);
	}
}

static void do_irq(int irqno)
{
	u16 vec;

	// some of these crash (program bug, not emulator bug -- well the
	// emulator shouldn't fire IRQs that weren't enabled.  until then,
	// just ignore these IRQs)
//	if (irqno == 1 || irqno == 2 || irqno == 5 || irqno == 6 || irqno == 7)
//		return;

	if (irqno == 8) {	// that's how we say "FIQ"
		if (fiq != 1)
			return;
		fiq |= 2;
		vec = 0xfff6;
		printf("### FIQ ###\n");
	} else {
		if (fiq & 2)
			return;
		if (irq != 1)
			return;
		irq |= 2;
		vec = 0xfff8 + irqno;
		//if (irqno)
		//	printf("### IRQ #%x ###\n", irqno);
	}

	u32 saved_sb = sb;
	push(reg[7], 0);
	push(reg[6], 0);
	reg[7] = load(vec);
	reg[6] = 0;

	int done;

//fprintf(stderr, "** RUN IRQ %d\n", irqno);
	for (done = 0; !done; ) {
		if (trace)
			print_state();

		if (mem[cs_pc()] == 0x9a98)	// RETI
			done = 1;

		step();
		insn_count++;
	}

	sb = saved_sb;

//fprintf(stderr, "** RUN IRQ %d DONE\n", irqno);
}

static void run_main(void)
{
	int i, idle, done;

	idle = 0;

//fprintf(stderr, "** RUN MAIN\n");
	for (i = 0, done = 0; i < 0x1000 && !done; i++) {
		if (trace)
			print_state();

		if (trace_new && ever_ran_this[cs_pc()] == 0) {
			ever_ran_this[cs_pc()] = 1;
			print_state();
		}

		if (cs_pc() == idle_pc) {
			idle++;
			if (idle == 2)
				done = 1;
		}

		step();
		insn_count++;
	}
//fprintf(stderr, "** RUN MAIN DONE\n");

	if (done)
		do_idle();
}

static void do_controller(void)
{
	char key;

	do {
		key = update_controller();

		switch (key) {
		case 0x1b:
			printf("Goodbye.\n");
			exit(0);

		case '1' ... '7':
			printf("*** doing IRQ %c\n", key);
			do_irq(key - '0');
			break;

		case 't':
			trace ^= 1;
			break;

		case 'y':
			trace_new ^= 1;
			break;

		case 'u':
			pause_after_every_frame ^= 1;
			break;

		case 'v':
			dump(0x2800, 0x80);
			break;

		case 'x':
			dump(0, 0x4000);
			break;
		}
	} while (key);
}

static void run(void)
{
	run_main();

	if (irq != 1)
		return;

	// FIXME
	if (insn_count < 1000000)
		return;

	struct timeval tv;
	u32 now;

	gettimeofday(&tv, 0);
	now = 1000000*tv.tv_sec + tv.tv_usec;

	if (now - last_retrace_time >= PERIOD) {
		// video
		static u32 which = 1;

		mem[0x2863] = mem[0x2862] & which;
		which ^= 3;

		if (mem[0x2863])
			do_irq(0);

		last_retrace_time = now;

		do_controller();

		// controller
		if (mem[0x3d21])
			do_irq(3);

		// sound
		//do_irq(4);	// XXX: gate me

		if (pause_after_every_frame) {
			printf("*** paused, press a key to continue\n");

			while (update_controller() == 0)
				;
		}
	}

	// flip some I/O reg bits
//	if (1 || (insn_count & 0x0fff) == 0) {
//		u16 addr = 0x3d00 + (random() % 0x100);
//		u16 val = random();
//		//printf("MAKE A MESS: %04x to %04x (was %04x)\n", val, addr, mem[addr]);
//		mem[addr] = val;
//	}


//	// progress report
//	if ((insn_count & 0x000fffff) == 0)
//		print_state();
}

void emu(void)
{
	switch_bank(0);

	memset(reg, 0, sizeof reg);
	reg[7] = mem[0xfff7];	// reset vector

	for (;;)
		run();
}
