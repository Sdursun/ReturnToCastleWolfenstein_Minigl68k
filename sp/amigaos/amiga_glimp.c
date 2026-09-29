/*
 * AmigaOS 3.x display code on top of minigl.library (PiStorm3D and any
 * other minigl.library implementation).
 *
 * Based on the MorphOS port by Mark Olsen.
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
 */

#include <exec/exec.h>
#include <intuition/intuition.h>

#include <proto/exec.h>
#include <proto/intuition.h>

#include "../renderer/tr_local.h"
#include "../client/client.h"

#include "amiga_glimp.h"

struct Window *amiga_inputwindow;
int mousevisible;

int amiga_speeds;
unsigned int amiga_drawMicros, amiga_drawCalls, amiga_drawIndexes;
unsigned int amiga_shadeMicros;
unsigned int amiga_soundMicros;

static cvar_t *r_amigaspeeds;
static cvar_t *r_mglbuffers;

static int glctx;
static int miniglopen;

/* Blank 16x1 sprite for hiding the pointer. Sprite data must be in chip
   RAM: two control words, one line of image data, two terminator words. */
static UWORD *blankpointer;

static void stub_glMultiTexCoord2fARB(GLenum unit, GLfloat s, GLfloat t)
{
	glMultiTexCoord2fARB(unit, s, t);
}

static void stub_glActiveTextureARB(GLenum unit)
{
	glActiveTextureARB(unit);
}

static void stub_glClientActiveTextureARB(GLenum unit)
{
	glClientActiveTextureARB(unit);
}

static void stub_glLockArraysEXT(GLint first, GLint count)
{
	glLockArrays(first, count);
}

static void stub_glUnlockArraysEXT(void)
{
	glUnlockArrays();
}

static void GLimp_HidePointer(void)
{
	if (amiga_inputwindow && blankpointer)
		SetPointer(amiga_inputwindow, blankpointer, 1, 16, 0, 0);

	mousevisible = 0;
}

static void GLimp_InitExtensions(void)
{
	GLint units;

	qglMultiTexCoord2fARB = 0;
	qglActiveTextureARB = 0;
	qglClientActiveTextureARB = 0;
	qglLockArraysEXT = 0;
	qglUnlockArraysEXT = 0;
	qglPNTrianglesiATI = 0;
	qglPNTrianglesfATI = 0;

	/* minigl.library has no gamma ramp, stencil buffer or GL_ADD. */
	glConfig.deviceSupportsGamma = qfalse;
	glConfig.textureEnvAddAvailable = qfalse;
	glConfig.textureCompression = TC_NONE;

	if (!r_allowExtensions->integer)
	{
		ri.Printf(PRINT_ALL, "*** IGNORING OPENGL EXTENSIONS ***\n");
		return;
	}

	/* MiniGL's multitexture and array locking are part of its core API
	   rather than advertised extensions, so ask for the unit count
	   directly. The query takes MiniGL's own enum value; the renderer's
	   GL_MAX_ACTIVE_TEXTURES_ARB (0x84E2) means nothing to it. */
	units = 0;
	glGetIntegerv(GL_MAX_TEXTURE_UNITS_ARB, &units);
	glConfig.maxActiveTextures = units;

	if (r_ext_multitexture->integer && units > 1)
	{
		qglMultiTexCoord2fARB = stub_glMultiTexCoord2fARB;
		qglActiveTextureARB = stub_glActiveTextureARB;
		qglClientActiveTextureARB = stub_glClientActiveTextureARB;
		ri.Printf(PRINT_ALL, "...using GL_ARB_multitexture (%d units)\n", units);
	}
	else
	{
		ri.Printf(PRINT_ALL, "...not using GL_ARB_multitexture\n");
	}

	if (r_ext_compiled_vertex_array->integer)
	{
		qglLockArraysEXT = stub_glLockArraysEXT;
		qglUnlockArraysEXT = stub_glUnlockArraysEXT;
		ri.Printf(PRINT_ALL, "...using GL_EXT_compiled_vertex_array\n");
	}
	else
	{
		ri.Printf(PRINT_ALL, "...not using GL_EXT_compiled_vertex_array\n");
	}
}

static qboolean GLimp_CreateContext(qboolean fullscreen)
{
	int colorbits;
	int depthbits;

	colorbits = r_colorbits->integer;
	if (colorbits != 16 && colorbits != 24 && colorbits != 32)
		colorbits = 16;

	depthbits = r_depthbits->integer;
	if (depthbits != 16 && depthbits != 24)
		depthbits = 24;

	mglChooseWindowMode(fullscreen ? GL_FALSE : GL_TRUE);
	mglChoosePixelDepth(colorbits);
	mglChooseZBufferDepth(depthbits);
	/* PiStorm3D: the buffer count, not mglEnableSync, decides whether
	   mglSwitchDisplay waits for the display. */
	r_mglbuffers = ri.Cvar_Get("r_mglbuffers", "2", CVAR_ARCHIVE | CVAR_LATCH);
	mglChooseNumberOfBuffers(r_mglbuffers->integer == 3 ? 3 : 2);
	/* The renderer submits batches of up to SHADER_MAX_VERTEXES vertices
	   through glDrawElements. */
	mglChooseVertexBufferSize(SHADER_MAX_VERTEXES * 2);

	ri.Printf(PRINT_ALL, "...creating %dx%d %s context, %d bit colour, %d bit Z\n",
		glConfig.vidWidth, glConfig.vidHeight, fullscreen ? "fullscreen" : "windowed",
		colorbits, depthbits);

	if (!mglCreateContext(0, 0, glConfig.vidWidth, glConfig.vidHeight))
		return qfalse;

	glConfig.colorBits = colorbits;
	glConfig.depthBits = depthbits;
	glConfig.stencilBits = 0;
	glConfig.isFullscreen = fullscreen;

	return qtrue;
}

void GLimp_Init(void)
{
	qboolean fullscreen;

	ri.Printf(PRINT_ALL, "Initializing minigl.library display\n");

	r_amigaspeeds = ri.Cvar_Get("r_amigaspeeds", "0", 0);

	if (!R_GetModeInfo(&glConfig.vidWidth, &glConfig.vidHeight, &glConfig.windowAspect, r_mode->integer))
	{
		/* r_mode -2 (desktop size) and bad values fall back to 640x480. */
		glConfig.vidWidth = 640;
		glConfig.vidHeight = 480;
		glConfig.windowAspect = 1.0f;
	}

	if (!MiniGLOpen())
	{
		Sys_Error("Unable to open minigl.library (version %d or newer required)\n", MINIGL_VERSION);
		return;
	}
	miniglopen = 1;

	fullscreen = r_fullscreen->integer ? qtrue : qfalse;

	if (!GLimp_CreateContext(fullscreen))
	{
		ri.Printf(PRINT_ALL, "...failed, trying the other display mode\n");

		fullscreen = !fullscreen;
		if (!GLimp_CreateContext(fullscreen))
		{
			GLimp_Shutdown();
			Sys_Error("Unable to create a MiniGL context\n");
			return;
		}
	}
	glctx = 1;

	ri.Cvar_Set("r_fullscreen", fullscreen ? "1" : "0");
	r_fullscreen->modified = qfalse;

	mglEnableSync(r_swapInterval->integer ? GL_TRUE : GL_FALSE);

	/* Ask MiniGL to lock the display only while a frame is drawn. It still
	   holds the lock between frames (ClearPointer locked up on quit), so
	   nothing that needs the display -- Shell output, Intuition calls -- may
	   run while the context is open; see Sys_Print and GLimp_Frame. */
	mglLockMode(MGL_LOCK_AUTOMATIC);
	amiga_holdconsole = 1;

	amiga_inputwindow = (struct Window *)mglGetInputWindowHandle();
	if (amiga_inputwindow)
	{
		/* Keep the right button for the game instead of the menu bar. */
		amiga_inputwindow->Flags |= WFLG_RMBTRAP;
	}

	blankpointer = AllocVec(6 * sizeof(UWORD), MEMF_CHIP | MEMF_CLEAR);
	GLimp_HidePointer();

	glConfig.driverType = GLDRV_ICD;
	glConfig.hardwareType = GLHW_GENERIC;

	Q_strncpyz(glConfig.vendor_string, (const char *)glGetString(GL_VENDOR), sizeof(glConfig.vendor_string));
	Q_strncpyz(glConfig.renderer_string, (const char *)glGetString(GL_RENDERER), sizeof(glConfig.renderer_string));
	Q_strncpyz(glConfig.version_string, (const char *)glGetString(GL_VERSION), sizeof(glConfig.version_string));
	Q_strncpyz(glConfig.extensions_string, (const char *)glGetString(GL_EXTENSIONS), sizeof(glConfig.extensions_string));

	GLimp_InitExtensions();
}

void GLimp_Shutdown(void)
{
	Amiga_Trace("GLimp_Shutdown");
	if (glctx)
	{
		/* No ClearPointer() here: it locked up the machine on quit, as MiniGL
		   still holds the display lock Intuition needs for it (see
		   GLimp_Frame). The window goes away with the context anyway. */
		amiga_inputwindow = 0;

		Amiga_Trace("mglDeleteContext");
		mglDeleteContext();
		Amiga_Trace("mglDeleteContext done");
		glctx = 0;
		amiga_holdconsole = 0;
		Amiga_FlushConsole();
	}

	if (blankpointer)
	{
		FreeVec(blankpointer);
		blankpointer = 0;
	}

	if (miniglopen)
	{
		Amiga_Trace("MiniGLClose");
		MiniGLClose();
		Amiga_Trace("MiniGLClose done");
		miniglopen = 0;
	}

	memset(&glConfig, 0, sizeof(glConfig));
	memset(&glState, 0, sizeof(glState));
}

void GLimp_LogComment(char *comment)
{
}

void GLimp_EndFrame(void)
{
	unsigned int start;

	if (!amiga_speeds)
	{
		mglSwitchDisplay();
		amiga_speeds = r_amigaspeeds->integer;
		return;
	}

	start = Amiga_Micros();
	mglSwitchDisplay();
	start = Amiga_Micros() - start;

	{
		/* surfaceType_t order, tr_local.h */
		static const char *names[SF_NUM_SURFACE_TYPES] = {
			"bad", "skip", "face", "grid", "tris", "poly", "md3", "mdc", "mds", "flare", "entity", "list"
		};
		char surfs[256];
		unsigned int shade = amiga_shadeMicros > amiga_drawMicros ? amiga_shadeMicros - amiga_drawMicros : 0;
		int i, len = 0;

		surfs[0] = 0;
		for (i = 0; i < SF_NUM_SURFACE_TYPES; i++)
		{
			if (amiga_surfCount[i] && len < (int)sizeof(surfs) - 32)
				len += snprintf(surfs + len, sizeof(surfs) - len, " %s %u.%u(%u)", names[i],
					amiga_surfMicros[i] / 1000, amiga_surfMicros[i] / 100 % 10, amiga_surfCount[i]);
			amiga_surfMicros[i] = amiga_surfCount[i] = 0;
		}

		/* shade: stage iterators without the draws; tessellation per surface
		   type in ms (count), which may include shading when the tess
		   buffer fills up mid-surface */
		ri.Printf(PRINT_ALL, "amiga: draw %u.%u (%u calls, %u idx) shade %u.%u swap %u.%u sound %u.%u |%s\n",
			amiga_drawMicros / 1000, amiga_drawMicros / 100 % 10, amiga_drawCalls, amiga_drawIndexes,
			shade / 1000, shade / 100 % 10,
			start / 1000, start / 100 % 10,
			amiga_soundMicros / 1000, amiga_soundMicros / 100 % 10, surfs);
	}

	amiga_drawMicros = amiga_drawCalls = amiga_drawIndexes = amiga_shadeMicros = amiga_soundMicros = 0;
	amiga_speeds = r_amigaspeeds->integer;
}

void *GLimp_RendererSleep(void)
{
	return 0;
}

qboolean GLimp_SpawnRenderThread(void (*function)(void))
{
	return qfalse;
}

void GLimp_FrontEndSleep(void)
{
}

void GLimp_WakeRenderer(void *data)
{
}

void GLimp_SetGamma(unsigned char red[256], unsigned char green[256], unsigned char blue[256])
{
}

/* Called once per frame from the main loop: show the pointer while the
   console is open in a window, so the user can get out of it. */
void GLimp_Frame(void)
{
	/* The pointer used to be shown while the console is open in a window.
	   SetPointer/ClearPointer need the display lock, which MiniGL keeps
	   between frames even in MGL_LOCK_AUTOMATIC mode (ClearPointer in
	   GLimp_Shutdown locked up the machine), so the pointer stays hidden
	   for as long as the context is open. */
}
