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

/* AmigaOS 3.x input, based on the MorphOS port's input.device handler. */

#include <exec/exec.h>
#include <exec/interrupts.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <devices/input.h>
#include <devices/inputevent.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/keymap.h>

#include <clib/alib_protos.h>

#include "../client/client.h"

#include "amiga_in.h"
#include "amiga_glimp.h"

/* NewMouse standard (wheel and extra buttons), not part of the NDK. */
#ifndef IECLASS_NEWMOUSE
#define IECLASS_NEWMOUSE 0x16
#endif
#ifndef NM_WHEEL_UP
#define NM_WHEEL_UP 0x7A
#endif
#ifndef NM_WHEEL_DOWN
#define NM_WHEEL_DOWN 0x7B
#endif
#ifndef NM_BUTTON_FOURTH
#define NM_BUTTON_FOURTH 0x7E
#endif

#define MAXIMSGS 32

/* Filled by the input handler, which runs in input.device's task. */
static struct InputEvent imsgs[MAXIMSGS];
static volatile int imsglow = 0;
static volatile int imsghigh = 0;

extern struct IntuitionBase *IntuitionBase;

static struct Interrupt InputHandler;
static struct MsgPort *inputport = 0;
static struct IOStdReq *inputreq = 0;
static BYTE inputret = -1;

static int mouse_x, mouse_y;

#define DEBUGRING(x)

unsigned char keyconv[] =
{
	'`', /* 0: the key left of 1 opens the console whatever the keymap puts there */
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0, /* 10 */
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0, /* 20 */
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0, /* 30 */
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0, /* 40 */
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0, /* 50 */
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0, /* 60 */
	0,
	0,
	0,
	0,
	K_BACKSPACE,
	K_TAB,
	K_KP_ENTER,
	K_ENTER,
	K_ESCAPE,
	K_DEL, /* 70 */
	K_INS,
	K_PGUP,
	K_PGDN,
	0,
	K_F11,
	K_UPARROW,
	K_DOWNARROW,
	K_RIGHTARROW,
	K_LEFTARROW,
	K_F1, /* 80 */
	K_F2,
	K_F3,
	K_F4,
	K_F5,
	K_F6,
	K_F7,
	K_F8,
	K_F9,
	K_F10,
	0, /* 90 */
	0,
	0,
	0,
	0,
	0,
	K_SHIFT,
	K_SHIFT,
	0,
	K_CTRL,
	K_ALT, /* 100 */
	K_ALT,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	K_PAUSE, /* 110 */
	K_F12,
	K_HOME,
	K_END,
	0,
	0,
	0,
	0,
	0,
	0,
	0, /* 120 */
	0,
	K_MWHEELUP,
	K_MWHEELDOWN,
	0,
	0,
	0,
	0,
	0,
	0,
	0, /* 130 */
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0, /* 140 */
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0, /* 150 */
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0, /* 160 */
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0, /* 170 */
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0, /* 180 */
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0, /* 190 */
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0, /* 200 */
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0, /* 210 */
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0, /* 220 */
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0, /* 230 */
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0, /* 240 */
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0, /* 250 */
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0
};

static struct InputEvent *myinputhandler(register struct InputEvent *moo __asm("a0"), register APTR data __asm("a1"));

void IN_Shutdown()
{
	if (inputret == 0)
	{
		inputreq->io_Data = (void *)&InputHandler;
		inputreq->io_Command = IND_REMHANDLER;
		DoIO((struct IORequest *)inputreq);

		CloseDevice((struct IORequest *)inputreq);

		inputret = -1;
	}

	if (inputreq)
	{
		DeleteStdIO(inputreq);

		inputreq = 0;
	}

	if (inputport)
	{
		DeletePort(inputport);

		inputport = 0;
	}

}

void IN_Init()
{
	inputport = CreatePort(0, 0);
	if (inputport == 0)
	{
		IN_Shutdown();
		Sys_Error("Unable to create message port");
	}

	inputreq = CreateStdIO(inputport);
	if (inputreq == 0)
	{
		IN_Shutdown();
		Sys_Error("Unable to create IO request");
	}

	inputret = OpenDevice("input.device", 0, (struct IORequest *)inputreq, 0);
	if (inputret != 0)
	{
		IN_Shutdown();
		Sys_Error("Unable to open input.device");
	}

	InputHandler.is_Node.ln_Type = NT_INTERRUPT;
	InputHandler.is_Node.ln_Pri = 100;
	InputHandler.is_Node.ln_Name = "Return to Castle Wolfenstein input handler";
	InputHandler.is_Data = 0;
	InputHandler.is_Code = (void(*)())myinputhandler;
	inputreq->io_Data = (void *)&InputHandler;
	inputreq->io_Command = IND_ADDHANDLER;
	DoIO((struct IORequest *)inputreq);

}

/* Events decoded from one input event but not handed out yet: a key
   press gives both SE_KEY and SE_CHAR. A queue rather than a single slot,
   because mouse movement accumulated in the same pass must not push a
   pending SE_CHAR out (typed text went missing in the console). */
#define MAXPENDING 16

static sysEvent_t pending[MAXPENDING];
static int pendinglow, pendinghigh;

static void QueueEvent(sysEventType_t type, int value, int value2)
{
	int next = (pendinghigh + 1) % MAXPENDING;

	if (next == pendinglow)
		return;	/* full, drop it */

	pending[pendinghigh].evType = type;
	pending[pendinghigh].evValue = value;
	pending[pendinghigh].evValue2 = value2;
	pendinghigh = next;
}

/* Decodes one raw input event into pending events. */
static void DecodeInputEvent(struct InputEvent *in)
{
	struct InputEvent ie;
	unsigned char key;
	int code, down;

	if (in->ie_Class == IECLASS_RAWKEY)
	{
		down = !(in->ie_Code & IECODE_UP_PREFIX);
		code = in->ie_Code & ~IECODE_UP_PREFIX;

		key = code <= 255 ? keyconv[code] : 0;

		if (key == 0)
		{
			bzero(&ie, sizeof(ie));

			ie.ie_Class = IECLASS_RAWKEY;
			ie.ie_SubClass = 0;
			ie.ie_Code = code;
			ie.ie_Qualifier = in->ie_Qualifier & ~(IEQUALIFIER_CONTROL);

			if (MapRawKey(&ie, (STRPTR)&key, 1, 0) != 1)
				key = 0;
		}

		if (key)
		{
			QueueEvent(SE_KEY, key, down);

			/* Printable keys (and backspace) also type a character. */
			if (down && (keyconv[code] == 0 || key == K_BACKSPACE))
				QueueEvent(SE_CHAR, key == K_BACKSPACE ? 8 : key, 0);
		}
	}
	else if (in->ie_Class == IECLASS_RAWMOUSE)
	{
		code = in->ie_Code & ~IECODE_UP_PREFIX;
		down = !(in->ie_Code & IECODE_UP_PREFIX);

		if (code == IECODE_LBUTTON)
			QueueEvent(SE_KEY, K_MOUSE1, down);
		else if (code == IECODE_RBUTTON)
			QueueEvent(SE_KEY, K_MOUSE2, down);
		else if (code == IECODE_MBUTTON)
			QueueEvent(SE_KEY, K_MOUSE3, down);

		mouse_x += in->ie_position.ie_xy.ie_x;
		mouse_y += in->ie_position.ie_xy.ie_y;
	}
	else if (in->ie_Class == IECLASS_NEWMOUSE)
	{
		/* NewMouse drivers send the wheel both as this class and as
		   RAWKEY 0x7A/0x7B; the RAWKEY copy is the one handled
		   (keyconv), so taking it here too would double it. */
		if (in->ie_Code == NM_BUTTON_FOURTH)
			QueueEvent(SE_KEY, K_MOUSE4, qtrue);
		else if (in->ie_Code == (NM_BUTTON_FOURTH | IECODE_UP_PREFIX))
			QueueEvent(SE_KEY, K_MOUSE4, qfalse);
	}
}

int IN_GetEvent(sysEvent_t *ev)
{
	int i;

	/* Decode what input.device has delivered, while there is room for the
	   two events a key press can produce; the rest waits in imsgs. */
	while (imsglow != imsghigh
	       && (pendinglow - pendinghigh - 1 + MAXPENDING) % MAXPENDING >= 2)
	{
		i = imsglow;

		if (amiga_inputwindow && (amiga_inputwindow->Flags & WFLG_WINDOWACTIVE))
			DecodeInputEvent(&imsgs[i]);

		/* One store, so the handler never sees an out-of-range index. */
		imsglow = (i + 1) % MAXIMSGS;
	}

	if (pendinglow != pendinghigh)
	{
		ev->evType = pending[pendinglow].evType;
		ev->evValue = pending[pendinglow].evValue;
		ev->evValue2 = pending[pendinglow].evValue2;
		pendinglow = (pendinglow + 1) % MAXPENDING;

		return 1;
	}

	if (mouse_x != 0 || mouse_y != 0)
	{
		ev->evType = SE_MOUSE;
		ev->evValue = mouse_x;
		ev->evValue2 = mouse_y;

		mouse_x = 0;
		mouse_y = 0;

		return 1;
	}

	return 0;
}

/* Runs in input.device's task, called with the event chain in A0. It
   copies the events it wants into the ring buffer and, while the game has
   the mouse, zeroes the movement so the Intuition pointer stays put. */
static struct InputEvent *myinputhandler(register struct InputEvent *moo __asm("a0"), register APTR data __asm("a1"))
{
	struct InputEvent *coin;
	struct Window *window;
	int screeninfront;
	int next;

	window = amiga_inputwindow;

	if (window == 0)
		return moo;

	screeninfront = window->WScreen == IntuitionBase->FirstScreen;

	for (coin = moo; coin; coin = coin->ie_NextEvent)
	{
		if (coin->ie_Class == IECLASS_RAWMOUSE || coin->ie_Class == IECLASS_RAWKEY || coin->ie_Class == IECLASS_NEWMOUSE)
		{
			next = (imsghigh + 1) % MAXIMSGS;
			if (next != imsglow)
			{
				CopyMem(coin, &imsgs[imsghigh], sizeof(imsgs[0]));
				imsghigh = next;
			}

			if (!mousevisible && (window->Flags & WFLG_WINDOWACTIVE) && coin->ie_Class == IECLASS_RAWMOUSE && screeninfront && window->MouseX > 0 && window->MouseY > 0)
			{
				coin->ie_position.ie_xy.ie_x = 0;
				coin->ie_position.ie_xy.ie_y = 0;
			}
		}
	}

	return moo;
}
