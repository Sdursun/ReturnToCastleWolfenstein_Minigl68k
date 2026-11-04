/*
 * The game's clock.
 *
 * The engine reads the time all the time, and on a PiStorm the usual ways are
 * slow: GetSysTime and ReadEClock go to the CIA over the 68k bus. Emu68 offers
 * the ARM's own counter through control registers, so that is used when Emu68
 * is there. Everywhere else (WinUAE, a real 68k, other accelerators) those
 * registers do not exist and reading them could crash, so the check comes first
 * and timer.device does the work.
 *
 * timer.device is called directly rather than through gettimeofday, because how
 * good libnix's gettimeofday is depends on the toolchain: the libnix of gcc 6.5.0b
 * 20260819 steps in 20 ms.
 *
 * Kept apart from amiga_main.c, as devices/timer.h and sys/time.h both define
 * struct timeval.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <stdlib.h>
#include <string.h>

#include <exec/exec.h>
#include <devices/timer.h>

/* libnix has a TimerBase of its own for gettimeofday. */
#define TimerBase amiga_TimerBase

#include <proto/exec.h>
#include <proto/timer.h>

struct Device *TimerBase;

void Sys_Error(const char *fmt, ...);

/* Filled in supervisor mode by the two routines below. Not static: the assembly
   refers to them by name. */
ULONG amiga_cntLo, amiga_cntHi, amiga_cntFrq;

void Amiga_ReadCnt(void);
void Amiga_ReadFrq(void);

/* movec is privileged, hence Supervisor() and rte. Encoded by hand, as the
   assembler has no names for these registers: 0xe0 CNTFRQ, 0xe1 CNTVALLO, 0xe2
   CNTVALHI (Emu68's docs/internals/ControlRegisters.md). The high word is read
   twice in case the low word wraps between the reads. */
__asm__(
	"_Amiga_ReadCnt:\n"
	"	movem.l	%d0-%d1,-(%sp)\n"
	"1:	.short	0x4e7a,0x00e2\n"	/* movec cntvalhi,d0 */
	"	.short	0x4e7a,0x10e1\n"	/* movec cntvallo,d1 */
	"	move.l	%d1,_amiga_cntLo\n"
	"	.short	0x4e7a,0x10e2\n"	/* movec cntvalhi,d1 */
	"	cmp.l	%d0,%d1\n"
	"	bne.s	1b\n"
	"	move.l	%d0,_amiga_cntHi\n"
	"	movem.l	(%sp)+,%d0-%d1\n"
	"	rte\n"
	"_Amiga_ReadFrq:\n"
	"	move.l	%d0,-(%sp)\n"
	"	.short	0x4e7a,0x00e0\n"	/* movec cntfrq,d0 */
	"	move.l	%d0,_amiga_cntFrq\n"
	"	move.l	(%sp)+,%d0\n"
	"	rte\n");

enum { CLOCK_NONE, CLOCK_EMU68, CLOCK_TIMERDEVICE };

static int clockmode = CLOCK_NONE;

static unsigned long long cntbase;
static ULONG cntperms, cntperus;

static struct timerequest timereq;
static ULONG sysbase;

static unsigned long long ReadCounter(void)
{
	Supervisor((ULONG (*)())Amiga_ReadCnt);
	return ((unsigned long long)amiga_cntHi << 32) | amiga_cntLo;
}

static void CloseTimer(void)
{
	if (TimerBase)
	{
		CloseDevice((struct IORequest *)&timereq);
		TimerBase = 0;
	}
}

static void InitClock(void)
{
	if (OpenResource((CONST_STRPTR)"devicetree.resource"))
	{
		Supervisor((ULONG (*)())Amiga_ReadFrq);

		/* Below 1 MHz the divisions lose their meaning. */
		if (amiga_cntFrq >= 1000000)
		{
			cntperms = amiga_cntFrq / 1000;
			cntperus = amiga_cntFrq / 1000000;
			cntbase = ReadCounter();
			clockmode = CLOCK_EMU68;
			return;
		}
	}

	memset(&timereq, 0, sizeof(timereq));
	if (OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_VBLANK, (struct IORequest *)&timereq, 0))
		Sys_Error("Unable to open timer.device\n");

	TimerBase = timereq.tr_node.io_Device;
	atexit(CloseTimer);

	{
		struct timeval tv;

		GetSysTime(&tv);
		sysbase = tv.tv_secs;
	}
	clockmode = CLOCK_TIMERDEVICE;
}

/* Which clock is in use, for the log. */
const char *Amiga_ClockName(void)
{
	if (clockmode == CLOCK_NONE)
		InitClock();

	return clockmode == CLOCK_EMU68 ? "Emu68 ARM counter" : "timer.device";
}

int Sys_Milliseconds(void)
{
	struct timeval tv;

	if (clockmode == CLOCK_EMU68)
		return (int)((ReadCounter() - cntbase) / cntperms);

	if (clockmode == CLOCK_NONE)
	{
		InitClock();
		return Sys_Milliseconds();
	}

	GetSysTime(&tv);
	return (int)((tv.tv_secs - sysbase) * 1000 + tv.tv_micro / 1000);
}

/* Microseconds, wrapping; for measuring short intervals (r_amigaspeeds). */
unsigned int Amiga_Micros(void)
{
	struct timeval tv;

	if (clockmode == CLOCK_EMU68)
		return (unsigned int)((ReadCounter() - cntbase) / cntperus);

	if (clockmode == CLOCK_NONE)
	{
		InitClock();
		return Amiga_Micros();
	}

	GetSysTime(&tv);
	return (tv.tv_secs - sysbase) * 1000000u + tv.tv_micro;
}
