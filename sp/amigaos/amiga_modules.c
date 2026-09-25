/*
 * Statically linked game modules for AmigaOS 3.x.
 *
 * AmigaOS 3.x has no shared objects, so qagame, cgame and ui are linked
 * into the executable. The Makefile prefixes every global symbol of each
 * module with the module name, which keeps their private copies of
 * q_shared.c, bg_*.c and ui_shared.c apart, and brackets each module's
 * .data and .bss with marker symbols.
 *
 * The engine expects a freshly loaded module every time it loads one (a
 * new map, a vid_restart, loading a saved game), just as a DLL would be.
 * So on every load after the first, the module's .data is restored from a
 * copy taken before it first ran and its .bss is cleared.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <stdlib.h>
#include <string.h>

#include "../game/q_shared.h"
#include "../qcommon/qcommon.h"

typedef int (*vmmain_t)(int, ...);
typedef void (*dllentry_t)(int (*syscallptr)(int, ...));

#define DECLARE_MODULE(name) \
	extern int name##_vmMain(int, ...); \
	extern void name##_dllEntry(int (*syscallptr)(int, ...)); \
	extern int name##_mod_data_begin, name##_mod_data_end; \
	extern int name##_mod_bss_begin, name##_mod_bss_end;

DECLARE_MODULE(qagame)
DECLARE_MODULE(cgame)
DECLARE_MODULE(ui)

typedef struct
{
	const char *name;
	vmmain_t vmMain;
	dllentry_t dllEntry;
	char *data_begin;
	char *data_end;
	char *bss_begin;
	char *bss_end;

	void *datacopy;
	qboolean loaded;
} staticmodule_t;

#define MODULE(name) \
	{ #name, (vmmain_t)name##_vmMain, name##_dllEntry, \
	  (char *)&name##_mod_data_begin, (char *)&name##_mod_data_end, \
	  (char *)&name##_mod_bss_begin, (char *)&name##_mod_bss_end, 0, qfalse }

static staticmodule_t modules[] =
{
	MODULE(qagame),
	MODULE(cgame),
	MODULE(ui),
};

#define NUM_MODULES (sizeof(modules) / sizeof(modules[0]))

void *Sys_LoadDll(const char *name, int (**entryPoint)(int, ...), struct SharedObjectSegments *sos)
{
	staticmodule_t *mod;
	unsigned int datasize;
	int i;

	/* Leave sos empty: the module is reset here, not by vm.c. */
	memset(sos, 0, sizeof(*sos));

	for (i = 0; i < NUM_MODULES; i++)
	{
		if (Q_stricmp(modules[i].name, name) == 0)
			break;
	}

	if (i == NUM_MODULES)
	{
		Com_Printf("Sys_LoadDll: no built-in module \"%s\"\n", name);
		return 0;
	}

	mod = &modules[i];

	if (mod->loaded)
	{
		Com_Printf("Sys_LoadDll: module \"%s\" is already loaded\n", name);
		return 0;
	}

	datasize = mod->data_end - mod->data_begin;

	if (mod->datacopy == 0)
	{
		/* First load: nothing in the module has run yet. */
		mod->datacopy = malloc(datasize);
		if (mod->datacopy == 0)
		{
			Com_Printf("Sys_LoadDll: out of memory for \"%s\"\n", name);
			return 0;
		}

		memcpy(mod->datacopy, mod->data_begin, datasize);
	}
	else
	{
		memcpy(mod->data_begin, mod->datacopy, datasize);
		memset(mod->bss_begin, 0, mod->bss_end - mod->bss_begin);
	}

	Com_Printf("Sys_LoadDll(%s): built-in, %u bytes data, %u bytes bss\n", name,
		datasize, (unsigned int)(mod->bss_end - mod->bss_begin));

	mod->loaded = qtrue;
	*entryPoint = mod->vmMain;

	return mod;
}

void Sys_UnloadDll(void *handle)
{
	staticmodule_t *mod = handle;

	if (mod)
		mod->loaded = qfalse;
}

qboolean Sys_InitDll(void *handle, int (*systemcalls)(int, ...))
{
	staticmodule_t *mod = handle;

	mod->dllEntry(systemcalls);

	return qtrue;
}

void Sys_DeinitDll(void *handle)
{
}
