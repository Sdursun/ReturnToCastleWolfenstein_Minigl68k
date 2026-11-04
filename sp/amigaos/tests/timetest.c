/*
 * Checks the game's clock (amiga_time.c) on the machine it runs on: which clock
 * was chosen, what a read costs against libnix's gettimeofday, whether it ever
 * runs backwards, and whether it keeps time against dos.library's Delay.
 *
 * Build, from sp/amigaos:
 *   m68k-amigaos-gcc -O2 -noixemul -m68040 -m68881 tests/timetest.c amiga_time.c
 *     -o timetest -lm
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/time.h>

#include <proto/dos.h>

#define CALLS 200000

int Sys_Milliseconds(void);
unsigned int Amiga_Micros(void);
const char *Amiga_ClockName(void);

void Sys_Error(const char *fmt, ...)
{
	va_list va;

	va_start(va, fmt);
	vprintf(fmt, va);
	va_end(va);
	exit(20);
}

int main(void)
{
	struct timeval tv;
	int i, before, ms, last, backwards = 0, steps = 0;
	unsigned int us, usstart, smallest = 0xffffffffu;
	double costms, costmicros, costgtod;

	printf("Clock: %s\n", Amiga_ClockName());

	/* Costs, each measured by this clock, then checked against Delay below. */
	usstart = Amiga_Micros();
	last = Sys_Milliseconds();
	for (i = 0; i < CALLS; i++)
	{
		ms = Sys_Milliseconds();
		if (ms < last)
			backwards++;
		if (ms != last)
			steps++;
		last = ms;
	}
	costms = (double)(Amiga_Micros() - usstart) / CALLS;

	usstart = Amiga_Micros();
	for (i = 0; i < CALLS; i++)
		us = Amiga_Micros();
	costmicros = (double)(Amiga_Micros() - usstart) / CALLS;

	/* The smallest step Amiga_Micros can show. */
	last = 0;
	us = Amiga_Micros();
	for (i = 0; i < 2000; i++)
	{
		unsigned int now = Amiga_Micros();

		if (now != us && now - us < smallest)
			smallest = now - us;
		us = now;
	}

	usstart = Amiga_Micros();
	for (i = 0; i < CALLS / 10; i++)
		gettimeofday(&tv, NULL);
	costgtod = (double)(Amiga_Micros() - usstart) / (CALLS / 10);

	printf("Sys_Milliseconds: %.2f us a call, %d steps in %d calls, %d backwards\n",
		costms, steps, CALLS, backwards);
	printf("Amiga_Micros:     %.2f us a call, smallest step %u us\n", costmicros, smallest);
	printf("gettimeofday:     %.2f us a call\n", costgtod);

	before = Sys_Milliseconds();
	Delay(5 * 50);
	ms = Sys_Milliseconds() - before;
	printf("Delay of 5000 ms took %d ms by this clock\n", ms);

	if (backwards)
		printf("FAILED: the clock ran backwards\n");
	else if (ms < 4900 || ms > 5200)
		printf("FAILED: the clock does not keep time\n");
	else
		printf("PASSED\n");

	return backwards ? 10 : 0;
}
