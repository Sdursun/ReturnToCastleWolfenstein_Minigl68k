/*
 * Copyright (C) 2005,2010 Mark Olsen
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <exec/exec.h>
#include <exec/system.h>
#include <intuition/intuition.h>
#include <dos/dosextens.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/dos.h>

#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include <dlfcn.h>

#include "../game/q_shared.h"
#include "../qcommon/qcommon.h"
#include "../renderer/tr_public.h"

#include "morphos_in.h"
#include "morphos_glimp.h"

#undef vsnprintf

#define MAXIMSGS 32

#ifdef DEDICATED
#define BINNAME "rtcw-mp"
#else
#define BINNAME "rtcwded-mp"
#endif

const char verstring[] = "$VER: "BINNAME" 1.0 (15.8.2010) By Mark Olsen, based on Return to Castle Wolfenstein by Id Software";

int __stack = 1024*1024;

static int consoleinput;
static int consoleoutput;
static int consoleoutputinteractive;

static BPTR olddir;

int use_altivec;
struct Library *SocketBase;
struct Library *DynLoadBase;

/* For NET_Sleep */
int stdin_active = 0;

static char *pakfiles[] =
{
	"main/pak0.pk3",
	"main/mp_pak0.pk3",
	"main/mp_pak1.pk3",
	"main/mp_pak2.pk3",
	"main/mp_pak3.pk3",
	"main/mp_pak4.pk3",
	"main/mp_pak5.pk3",
	0
};

void Sys_Quit()
{
	CL_Shutdown();
	IN_Shutdown();

	if (DynLoadBase)
		CloseLibrary(DynLoadBase);

	if (SocketBase)
		CloseLibrary(SocketBase);

	if (consoleinput && consoleoutputinteractive)
		SetMode(Input(), 0);

	if (olddir)
	{
		UnLock(CurrentDir(olddir));
	}

	exit(0);
}

static void ErrorMessage(char *string)
{
	char msg[4096];

	snprintf(msg, sizeof(msg), "%s", string);

	if (consoleoutput)
		fprintf(stderr, "%s\n", msg);
	else
	{
		struct EasyStruct es;

		if (msg[strlen(msg)-1] == '\n')
			msg[strlen(msg)-1] = 0;

		es.es_StructSize = sizeof(es);
		es.es_Flags = 0;
		es.es_Title = "Return to Castle Wolfenstein";
		es.es_TextFormat = msg;
		es.es_GadgetFormat = "Quit";

		EasyRequest(0, &es, 0, 0);
	}
}

void Sys_Error(const char *fmt, ...)
{
	va_list va;
	char msg[4096];

	strcpy(msg, "Sys_Error: ");

	va_start(va, fmt);
	vsnprintf(msg+strlen(msg), sizeof(msg)-strlen(msg), fmt, va);
	va_end(va);

	ErrorMessage(msg);

	Sys_Quit();
}

void Sys_Print(const char *msg)
{
	if (consoleoutput)
		fputs(msg, stdout);
}

qboolean Sys_LowPhysicalMemory()
{
	/* This is only used for touching memory, there's really no need for that... */

	return qtrue;
}

void Sys_BeginProfiling()
{
}

void Sys_InitStreamThread()
{
}

void Sys_ShutdownStreamThread()
{
}

void Sys_BeginStreamedFile(fileHandle_t f, int readAhead)
{
}

void Sys_EndStreamedFile(fileHandle_t f)
{
}

int Sys_StreamedRead(void *buffer, int size, int count, fileHandle_t f)
{
	return FS_Read( buffer, size * count, f );
}

void Sys_StreamSeek(fileHandle_t f, int offset, int origin)
{
	FS_Seek(f, offset, origin);
}

char *Sys_GetClipboardData()
{
	return 0;
}

int putenv __P((const char *name))
{
	return 1;
}

sysEvent_t Sys_GetEvent()
{
	sysEvent_t ev;
	static char inbuf[256];
	static int inbufsize;

	msg_t netmsg;
	byte netpacket[MAX_MSGLEN];
	netadr_t adr;

	memset(&ev, 0, sizeof(ev));
	ev.evTime = Sys_Milliseconds();

#ifndef DEDICATED
	if (IN_GetEvent(&ev))
		return ev;
#endif

	if (consoleinput && consoleoutputinteractive)
	{
		while(WaitForChar(Input(), 0) && Read(Input(), inbuf+inbufsize, 10) == 1)
		{
			if (inbuf[inbufsize] == 3)
			{
				Signal(FindTask(0), SIGBREAKF_CTRL_C);
				continue;
			}

			if (inbuf[inbufsize] == 8 && inbufsize != 0)
			{
				FPutC(Output(), 8);
				FPutC(Output(), ' ');
				FPutC(Output(), 8);
				Flush(Output());
				inbufsize--;
				continue;
			}

			if (inbuf[inbufsize] == '\r')
				inbuf[inbufsize] = '\n';
			else if (!isprint(inbuf[inbufsize]))
				continue;

			FPutC(Output(), inbuf[inbufsize]);
			Flush(Output());
			inbufsize++;
			if (inbuf[inbufsize-1] == '\n' || inbufsize == sizeof(inbuf)-11)
			{
				inbuf[inbufsize] = 0;
				ev.evType = SE_CONSOLE;
				ev.evPtr = Z_Malloc(inbufsize+1);
				ev.evPtrLength = inbufsize+1;

				strcpy(ev.evPtr, inbuf);

				inbufsize = 0;

				return ev;
			}
		}
	}
	else if (consoleinput)
	{
		while(WaitForChar(Input(), 0))
		{
			if (FGets(Input(), inbuf, sizeof(inbuf)))
			{
				ev.evType = SE_CONSOLE;
				ev.evPtr = Z_Malloc(strlen(inbuf)+1);
				ev.evPtrLength = strlen(inbuf)+1;

				strcpy(ev.evPtr, inbuf);

				return ev;
			}
		}
	}

	MSG_Init(&netmsg, netpacket, sizeof(netpacket));
	if (Sys_GetPacket(&adr, &netmsg))
	{
		netadr_t *buf;
		int len;

		len = sizeof(netadr_t) + netmsg.cursize;
		buf = Z_Malloc( len );
		*buf = adr;
		memcpy( buf+1, netmsg.data, netmsg.cursize );
		ev.evType = SE_PACKET;
		ev.evPtr = buf;
		ev.evPtrLength = len;

		return ev;
	}

	return ev;
}

qboolean Sys_CheckCD()
{
	return qtrue;
}

char *Sys_GetDLLName(const char *name)
{
	return va("%s_mp_morphos_ppc.so", name);
}

void *Sys_LoadDll(const char *name, char *fqpath, int (**entryPoint)(int, ...), struct SharedObjectSegments *sos)
{
	void *handle;
	char fname[MAX_OSPATH];
	char *basepath;
	char *gamedir;
	char *fn;
	int (*rtcwmp_so_init)(void);
	void (*rtcwmp_so_deinit)(void);
	void (*dllEntry)(int (*syscallptr)(int, ...));
	void (*so_get_segment_info)(void **sbss_segment_start, void **bss_segment_start, void **sdata_segment_start, void **data_segment_start, void **sbss_segment_end, void **bss_segment_end, void **sdata_segment_end, void **data_segment_end);
	void *so_sbss_segment_start;
	void *so_sbss_segment_end;
	void *so_bss_segment_start;
	void *so_bss_segment_end;
	void *so_sdata_segment_start;
	void *so_sdata_segment_end;
	void *so_data_segment_start;
	void *so_data_segment_end;

	memset(sos, 0, sizeof(*sos));

	*fqpath = 0;

	Q_strncpyz(fname, Sys_GetDLLName(name), sizeof(fname));

	basepath = Cvar_VariableString( "fs_basepath" );
	gamedir = Cvar_VariableString( "fs_game" );

	fn = FS_BuildOSPath( basepath, gamedir, fname );
	Com_Printf( "Sys_LoadDll(%s)...\n", fn );

	handle = dlopen(fn, RTLD_NOW);
	if (handle == 0)
	{
		Com_Printf("Sys_LoadDll: Failed to load \"%s\"\n", fn);
	}
	else
	{
		if (DynLoadBase->lib_Version == 51 && DynLoadBase->lib_Revision == 2)
		{
			/* Leet hax. Don't try this at home. */

			*(unsigned short *)(((void *)handle)+0x2a) = 0;
		}

		rtcwmp_so_init = dlsym(handle, "rtcwmp_so_init");
		rtcwmp_so_deinit = dlsym(handle, "rtcwmp_so_deinit");
		dllEntry = dlsym(handle, "dllEntry");
		*entryPoint = dlsym(handle, "vmMain");

		if (rtcwmp_so_init == 0 || rtcwmp_so_deinit == 0 || dllEntry == 0 || *entryPoint == 0)
		{
			Com_Printf("Sys_LoadDll: Missing symbols\n");
		}
		else
		{
			so_get_segment_info = dlsym(handle, "rtcw_so_get_segment_info");
			if (so_get_segment_info)
			{
				so_get_segment_info(&so_sbss_segment_start, &so_bss_segment_start, &so_sdata_segment_start, &so_data_segment_start, &so_sbss_segment_end, &so_bss_segment_end, &so_sdata_segment_end, &so_data_segment_end);

				if (so_sbss_segment_start && so_sbss_segment_end && so_bss_segment_start && so_bss_segment_end && so_sdata_segment_start && so_sdata_segment_end && so_data_segment_start && so_data_segment_end)
				{
					sos->sbss_segment_start = so_sbss_segment_start;
					sos->sbss_segment_size = so_sbss_segment_end - so_sbss_segment_start;
					sos->bss_segment_start = so_bss_segment_start;
					sos->bss_segment_size = so_bss_segment_end - so_bss_segment_start;
					sos->sdata_segment_start = so_sdata_segment_start;
					sos->sdata_segment_size = so_sdata_segment_end - so_sdata_segment_start;
					sos->data_segment_start = so_data_segment_start;
					sos->data_segment_size = so_data_segment_end - so_data_segment_start;
 				}
			}

			Q_strncpyz(fqpath, fn, MAX_QPATH);

			return handle;
		}

		dlclose(handle);
	}

	return 0;
}

void Sys_UnloadDll(void *handle)
{
	dlclose(handle);
}

qboolean Sys_InitDll(void *handle, int (*systemcalls)(int, ...))
{
	int (*rtcwmp_so_init)(void);
	void (*dllEntry)(int (*syscallptr)(int, ...));

	rtcwmp_so_init = dlsym(handle, "rtcwmp_so_init");
	dllEntry = dlsym(handle, "dllEntry");
	if (rtcwmp_so_init && dllEntry)
	{
		if (rtcwmp_so_init())
		{
			dllEntry(systemcalls);

			return qtrue;
		}
	}

	return qfalse;
}

void Sys_DeinitDll(void *handle)
{
	void (*rtcwmp_so_deinit)(void);

	rtcwmp_so_deinit = dlsym(handle, "rtcwmp_so_deinit");
	if (rtcwmp_so_deinit)
	{
		rtcwmp_so_deinit();
	}
}

static char *ProcessorName()
{
	ULONG CPUType = MACHINE_PPC;

	NewGetSystemAttrs(&CPUType,sizeof(CPUType),SYSTEMINFOTYPE_MACHINE,TAG_DONE);
	if (CPUType == MACHINE_PPC)
	{
		ULONG CPUVersion, CPURevision;

		if (NewGetSystemAttrs(&CPUVersion,sizeof(CPUVersion),SYSTEMINFOTYPE_PPC_CPUVERSION,TAG_DONE)
		 && NewGetSystemAttrs(&CPURevision,sizeof(CPURevision),SYSTEMINFOTYPE_PPC_CPUREVISION,TAG_DONE))
		{
			switch (CPUVersion)
			{
				case 0x0001:
					return "601";
				case 0x0003: 
					return "603";
				case 0x0004:
					return "604";
				case 0x0006:
					return "603E";
				case 0x0007:
					return "603R/603EV";
				case 0x0008:
					if ((CPURevision & 0xf000) == 0x2000)
					{
						if (CPURevision >= 0x2214)
							return "750CXE (G3)";
						else
							return "750CX (G3)";
					}
					else
						return "740/750 (G3)";
				case 0x0009:
					return "604E";
				case 0x000A:
					return "604EV";
				case 0x000C:
					if (CPURevision & 0x1000)
						return "7410 (G4)";
					else
						return "7400 (G4)";
				case 0x0039:
					return "970 (G5)";
				case 0x003C:
					return "970FX (G5)";
				case 0x8000:
					if (CPURevision > 0x0200)
						return "7451 (G4)";
					else
						return "7441/7450 (G4)";
				case 0x8001:
					return "7445/7455 (G4)";
				case 0x8002:
					return "7447/7457 (G4)";
				case 0x8003:
					return "7447A (G4)";
				case 0x8004:
					return "7448 (G4)";
				case 0x800C:
					return "7410 (G4)";
				default:
					return "Unknown";
			}
		}
	}

	return "Unknown";
}

void Sys_Init()
{
	Cvar_Set("arch", "morphos ppc");

	Cvar_Set("sys_cpustring", ProcessorName());

	IN_Init();
}

int main(int argc, char **argv)
{
	struct Resident *morphos;
	int r1, r2;

	int i, len;

	char *cmdline;

	BPTR l;

	extern struct WBStartup *_WBenchMsg;

	BPTR lock;

	if (!_WBenchMsg)
	{
		consoleoutput = 1;
		if (IsInteractive(Output()))
			consoleoutputinteractive = 1;
	}

	l = Lock("PROGDIR:", ACCESS_READ);
	if (l)
		olddir = CurrentDir(l);

	for(i=0;pakfiles[i] != 0;i++)
	{
		lock = Lock(pakfiles[i], ACCESS_READ);
		if (lock)
			UnLock(lock);
		else
			break;
	}

	if (pakfiles[i] != 0)
	{
		ErrorMessage("Incomplete installation. Please run the installer.\n");
		return 0;
	}

	morphos = FindResident("MorphOS");
	r1 = NewGetSystemAttrs(&r2, sizeof(r2), SYSTEMINFOTYPE_PPC_ALTIVEC, TAG_DONE);

	if (r1 == sizeof(r2) && r2 != 0 && morphos && (morphos->rt_Flags&RTF_EXTENDED) && (morphos->rt_Version > 1 || (morphos->rt_Version == 1 && morphos->rt_Revision >= 5)))
		use_altivec = 1;

	signal(SIGINT, SIG_IGN);

	DynLoadBase = OpenLibrary("dynload.library", 51);
	if (DynLoadBase && DynLoadBase->lib_Revision < 2)
	{
		CloseLibrary(DynLoadBase);
		DynLoadBase = 0;
	}
	if (DynLoadBase == 0)
	{
		ErrorMessage("Unable to open dynload.library version 51.2 or newer");
		return 0;
	}

	SocketBase = OpenLibrary("bsdsocket.library", 0);
#ifdef DEDICATED
	if (SocketBase == 0)
	{
		printf("Unable to open bsdsocket.library\n");
		CloseLibrary(DynLoadBase);
		return 0;
	}
#endif

	Sys_SetDefaultCDPath(argv[0]);

	len = 1;
	for(i=1;i<argc;i++)
		len+= strlen(argv[i])+1;

	cmdline = malloc(len);
	if (cmdline == 0)
		Sys_Quit();

	cmdline[0] = 0;

	for(i=1;i<argc;i++)
	{
		strcat(cmdline, argv[i]);
		if (i != argc-1)
			strcat(cmdline, " ");
	}

	Com_Init(cmdline);

	if (SocketBase)
		NET_Init();

	if (!_WBenchMsg && com_dedicated && com_dedicated->value && IsInteractive(Input()))
	{
		consoleinput = 1;
		if (consoleoutputinteractive)
			SetMode(Input(), 1);
	}

	while((SetSignal(0, 0)&SIGBREAKF_CTRL_C) == 0)
	{
		Com_Frame();
#ifndef DEDICATED
		GLimp_Frame();
#endif
	}

	Sys_Quit();

	return 0;
}

qboolean Sys_IsNumLockDown(void)
{
	return qfalse;
}

void Sys_OpenURL(const char *url, qboolean doexit)
{
}

void Sys_StartProcess(char *cmdline, qboolean doexit)
{
}
