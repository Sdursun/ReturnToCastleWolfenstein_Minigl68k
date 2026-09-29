/*
 * AmigaOS 3.x system code, based on the MorphOS port.
 *
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
#include <exec/execbase.h>
#include <intuition/intuition.h>
#include <dos/dosextens.h>
#include <workbench/startup.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/dos.h>

#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <setjmp.h>
#include <sys/time.h>
#include <ctype.h>

#include "../game/q_shared.h"
#include "../qcommon/qcommon.h"
#include "../renderer/tr_public.h"

#include "amiga_in.h"
#include "amiga_glimp.h"

#undef vsnprintf

/* Defined in amiga_net.c and unix_shared.c; not declared in a header. */
qboolean Sys_GetPacket(netadr_t *net_from, msg_t *net_message);
void Sys_SetDefaultCDPath(const char *path);

#define BINNAME "rtcw-sp"

const char verstring[] = "$VER: "BINNAME" 1.0 (24.9.2026) AmigaOS 3.x/MiniGL port, based on the MorphOS port by Mark Olsen";

/* Picked up by libnix's swapstack.o, which the Makefile links in. The
   game recurses deeply (BSP traversal, bot AI, script parsing). */
unsigned long __stack = 2 * 1024 * 1024;

extern struct ExecBase *SysBase;

static int consoleinput;
static int consoleoutput;
static int consoleoutputinteractive;

static BPTR olddir;

/* For NET_Sleep */
int stdin_active = 0;

static char *pakfiles[] =
{
	"main/pak0.pk3",
	"main/sp_pak1.pk3",
	"main/sp_pak2.pk3",
	"main/sp_pak3.pk3",
	0
};

/* Set in main(). Quitting jumps back there instead of calling exit() from
   deep inside the engine: libnix's swapstack copies everything between
   main's entry and the current stack pointer back onto the small Shell
   stack on exit, and from inside a command (quit from the menu) that can be
   more than a default Shell stack holds; overrunning it would explain the
   whole machine locking up on exit. */
static jmp_buf quitjmp;
static int quitjmpset;

/* Shutdown progress, with developer 1 only: written to stdout at once, so a
   log redirected to a file shows how far quitting got if the machine locks
   up (as it did before ClearPointer was removed from GLimp_Shutdown). */
void Amiga_Trace(const char *msg)
{
	char line[256];

	if (!com_developer || !com_developer->integer)
		return;

	snprintf(line, sizeof(line), "quit: %s\n", msg);
	Sys_Print(line);
	fflush(stdout);
}

static void ErrorMessage(char *string);

/* Set by Sys_Error: shown by Sys_Quit once the display is closed, as a
   requester or console output could otherwise lock up (see Sys_Print). */
static char quiterror[4096];

void Sys_Quit()
{
	Amiga_Trace("Sys_Quit");
	CL_Shutdown();
	Amiga_FlushConsole();

	if (quiterror[0])
		ErrorMessage(quiterror);

	Amiga_Trace("IN_Shutdown");
	IN_Shutdown();
	Amiga_Trace("IN_Shutdown done");

	if (consoleinput && consoleoutputinteractive)
		SetMode(Input(), 0);

	if (olddir)
	{
		UnLock(CurrentDir(olddir));
		olddir = 0;
	}

	if (quitjmpset)
		longjmp(quitjmp, 1);

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

		if (msg[0] && msg[strlen(msg)-1] == '\n')
			msg[strlen(msg)-1] = 0;

		es.es_StructSize = sizeof(es);
		es.es_Flags = 0;
		es.es_Title = (UBYTE *)"Return to Castle Wolfenstein";
		es.es_TextFormat = (UBYTE *)"%s";
		es.es_GadgetFormat = (UBYTE *)"Quit";

		EasyRequest(0, &es, 0, (ULONG)msg);
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

	Q_strncpyz(quiterror, msg, sizeof(quiterror));

	Sys_Quit();
}

/*
 * Console output while a MiniGL context is open.
 *
 * MiniGL keeps the display locked while a context is open, even between
 * frames. Writing to a Shell window meanwhile makes console.device wait
 * for that lock, which this task holds, so the machine locks up. When
 * stdout is a console, the output is therefore held back while a context
 * is open and written when it closes (GLimp_Shutdown calls
 * Amiga_FlushConsole). Output redirected to a file does not touch the
 * display and is written straight away, so a log survives a lock-up.
 */
int amiga_holdconsole;

static char heldoutput[262144];
static int heldlength;

void Amiga_FlushConsole(void)
{
	if (heldlength)
	{
		fwrite(heldoutput, 1, heldlength, stdout);
		fflush(stdout);
		heldlength = 0;
	}
}

void Sys_Print(const char *msg)
{
	int len;

	if (!consoleoutput)
		return;

	if (!(amiga_holdconsole && consoleoutputinteractive))
	{
		fputs(msg, stdout);
		return;
	}

	/* Keep what fits; a flood between two frames loses its tail. */
	len = strlen(msg);
	if (len > (int)sizeof(heldoutput) - heldlength)
		len = sizeof(heldoutput) - heldlength;

	memcpy(heldoutput + heldlength, msg, len);
	heldlength += len;
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

	if (IN_GetEvent(&ev))
		return ev;

	if (consoleinput && consoleoutputinteractive)
	{
		while(WaitForChar(Input(), 0) && Read(Input(), inbuf+inbufsize, 1) == 1)
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
			else if (!isprint((unsigned char)inbuf[inbufsize]))
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

static const char *ProcessorName(void)
{
	UWORD attn = SysBase->AttnFlags;

	/* Bit 10 is set by the Apollo 68080. Emu68 reports itself as a 68040. */
	if (attn & (1 << 10))
		return "68080";
	if (attn & (1 << 7))
		return "68060";
	if (attn & AFF_68040)
		return "68040";
	if (attn & AFF_68030)
		return "68030";
	if (attn & AFF_68020)
		return "68020";

	return "68000";
}

/* See amiga_layout.c. Returns 0 and reports the first difference if the
   game side and the OS side disagree about a shared structure. */
static int CheckStructLayout(void)
{
	extern const int *amiga_layout_engine(const char ***n);
	extern const int *amiga_layout_os(const char ***n);
	const int *engine, *os;
	const char **names;
	char msg[256];
	int i;

	engine = amiga_layout_engine(&names);
	os = amiga_layout_os(&names);

	for (i = 0; names[i]; i++)
	{
		if (engine[i] != os[i])
		{
			snprintf(msg, sizeof(msg), "Internal error: %s is %d bytes in the game code but %d bytes in the system code.\n", names[i], engine[i], os[i]);
			ErrorMessage(msg);
			return 0;
		}
	}

	return 1;
}

/* Free Fast RAM in MB, for sizing the pk3 RAM cache (files.c). */
int Sys_AmigaFreeFastMB(void)
{
	return (int)(AvailMem(MEMF_FAST) >> 20);
}

/* Microseconds, wrapping; for measuring short intervals (r_amigaspeeds). */
unsigned int Amiga_Micros(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	return (unsigned int)tv.tv_sec * 1000000u + (unsigned int)tv.tv_usec;
}

/* Reports the stack actually in use, to tell whether libnix's swapstack
   gave us __stack bytes. MiniGL warns about a small stack at startup; it
   may be looking at the Shell's setting (pr_StackSize) instead. */
static void PrintStackInfo(void)
{
	struct Process *pr = (struct Process *)FindTask(0);
	char *lower = (char *)pr->pr_Task.tc_SPLower;
	char *upper = (char *)pr->pr_Task.tc_SPUpper;
	char here;

	Com_DPrintf("Stack: %ld bytes in use by this task (%ld bytes used so far), Shell stack setting %ld bytes, requested %lu\n",
		(long)(upper - lower), (long)(upper - &here), (long)pr->pr_StackSize, __stack);
}

void Sys_Init()
{
	PrintStackInfo();

	Cvar_Set("arch", "amigaos m68k");

	Cvar_Set("sys_cpustring", (char *)ProcessorName());

	IN_Init();
}

int main(int argc, char **argv)
{
	int i, len;

	char *cmdline;

	BPTR l;

	extern struct WBStartup *_WBenchMsg;

	BPTR lock;

	/* Line buffered, so a log redirected to a file survives a lock-up. */
	setvbuf(stdout, NULL, _IOLBF, BUFSIZ);

	/* Sys_Quit comes back here, with the stack shallow again. */
	if (setjmp(quitjmp))
	{
		Amiga_Trace("back in main, leaving to the C library");
		return 0;
	}
	quitjmpset = 1;

	if (!_WBenchMsg)
	{
		consoleoutput = 1;
		if (IsInteractive(Output()))
			consoleoutputinteractive = 1;
	}

	/* The code needs at least a 68020 with an FPU. */
	if (!(SysBase->AttnFlags & AFF_68020) || !(SysBase->AttnFlags & (AFF_68881 | AFF_68882 | AFF_FPU40)))
	{
		ErrorMessage("Return to Castle Wolfenstein needs a 68040 or better with an FPU.\n");
		return 20;
	}

	if (!CheckStructLayout())
		return 20;

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
		char msg[256];

		snprintf(msg, sizeof(msg), "Incomplete installation: %s is missing.\nCopy the main/ directory of a patched RTCW installation next to this program.\n", pakfiles[i]);
		ErrorMessage(msg);

		if (olddir)
			UnLock(CurrentDir(olddir));

		return 20;
	}

	signal(SIGINT, SIG_IGN);

	/* No CD path: the MorphOS code passed argv[0], which made "rtcw-sp/main" a
	   search path, probed on disk for every file the game cannot find. */
	Sys_SetDefaultCDPath("");

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
		GLimp_Frame();
	}

	Sys_Quit();

	return 0;
}

qboolean Sys_IsNumLockDown(void)
{
	return qfalse;
}

void Sys_OpenURL(char *url, qboolean doexit)
{
}

void Sys_StartProcess(char *cmdline, qboolean doexit)
{
}
