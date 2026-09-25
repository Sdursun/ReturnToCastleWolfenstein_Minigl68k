/*
 * Public part of the PiStorm3D SDK's mgl/context.h; see ../README.txt.
 * Included by mgl/gl.h after the GL types are defined.
 *
 * Derived from the PiStorm3D SDK headers, which are under the Hyperion
 * MiniGL Open Source License (PiStorm3D/licences/License_MiniGL_Library.txt
 * in the SDK): MiniGL may only be used on AmigaOS, and modifications must
 * be made publicly available to the Amiga developer community.
 */

#ifndef __CONTEXT_H
#define __CONTEXT_H

#include "mgl/matrix.h"
#include "mgl/config.h"
#include "mgl/vertexbuffer.h"

/* The real header gets these from <intuition/intuition.h>; gl.h and the
   dispatch table use them in prototypes. */
struct Window;
struct BitMap;

typedef struct GLcontext_t * GLcontext;

struct GLcontext_t;

typedef void (*DrawFn)(struct GLcontext_t *);

typedef enum
{
	MGLKEY_F1, MGLKEY_F2, MGLKEY_F3, MGLKEY_F4, MGLKEY_F5, MGLKEY_F6, MGLKEY_F7, MGLKEY_F8,
	MGLKEY_F9, MGLKEY_F10,
	MGLKEY_CUP, MGLKEY_CDOWN, MGLKEY_CLEFT, MGLKEY_CRIGHT
} MGLspecial;

typedef void (*KeyHandlerFn)(char key);
typedef void (*SpecialHandlerFn)(MGLspecial special_key);
typedef void (*MouseHandlerFn)(GLint x, GLint y, GLbitfield buttons);
typedef void (*IdleFn)(void);

#endif
