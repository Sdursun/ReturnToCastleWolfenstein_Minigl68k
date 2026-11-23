/*
 * libwhere: which library's code is at an address. For every library and
 * device in memory, the targets of its jump table (JMP abs.l entries) give
 * the range its functions lie in; prints the ones whose range is near the
 * given address, or all of them.
 *
 *   libwhere [hex address]
 *
 * Used to name the "outside the program" PC samples of pcsample.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */
#include <stdio.h>
#include <stdlib.h>
#include <exec/execbase.h>
#include <exec/libraries.h>
#include <proto/exec.h>

extern struct ExecBase *SysBase;

static void Scan(struct List *list, const char *kind, unsigned long addr)
{
	struct Node *n;

	for (n = list->lh_Head; n->ln_Succ; n = n->ln_Succ)
	{
		struct Library *lib = (struct Library *)n;
		unsigned short *p = (unsigned short *)lib;
		unsigned long lo = 0xffffffffUL, hi = 0;
		int i, count = lib->lib_NegSize / 6;

		for (i = 1; i <= count; i++)
		{
			unsigned short *e = (unsigned short *)((char *)lib - i * 6);
			unsigned long t;

			if (e[0] != 0x4ef9)
				continue;
			t = ((unsigned long)e[1] << 16) | e[2];
			if (t < lo)
				lo = t;
			if (t > hi)
				hi = t;
		}
		(void)p;
		if (hi == 0)
			continue;
		if (addr == 0 || (addr >= lo - 0x10000 && addr <= hi + 0x40000))
			printf("%-8s %-32s %08lx-%08lx  base %08lx\n", kind, n->ln_Name ? n->ln_Name : "?", lo, hi,
			       (unsigned long)lib);
	}
}

int main(int argc, char **argv)
{
	unsigned long addr = argc > 1 ? strtoul(argv[1], NULL, 16) : 0;

	Forbid();
	Scan(&SysBase->LibList, "library", addr);
	Scan(&SysBase->DeviceList, "device", addr);
	Permit();
	return 0;
}
