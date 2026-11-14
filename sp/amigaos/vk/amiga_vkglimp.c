/*
 * AmigaOS 3.x display code on top of vulkan.library (Vulkan 1.1: VulkanV3D
 * on the PiStorm's Pi 4 GPU, QuarkTex NG in WinUAE): the Vulkan build,
 * rtcw-sp-vk. An experiment; the MiniGL build is amiga_glimp.c, and this
 * file follows it. Taken over from the Jedi Outcast port (jk2sp_vk).
 *
 * Full screen: a B G R A screen of the mode's size with a backdrop window
 * for input. Windowed: a window on the Workbench, which has to be a 32-bit
 * screen.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <exec/exec.h>
#include <intuition/intuition.h>

#include <proto/exec.h>
#include <proto/intuition.h>

#include "../../renderer/tr_local.h"
#include "../../client/client.h"

#include "../amiga_glimp.h"
#include "amiga_vk.h"

struct Window *amiga_inputwindow;
int mousevisible;

int amiga_speeds;
unsigned int amiga_drawMicros, amiga_drawCalls, amiga_drawIndexes;
unsigned int amiga_drawMaxVerts, amiga_drawMaxIndexes, amiga_drawOverVerts;
unsigned int amiga_shadeMicros;
unsigned int amiga_soundMicros;

static cvar_t *r_amigaspeeds;

/* r_vkmsaa 4: 4x multisampling. r_vkgamma 1: the game's gamma ramp (r_gamma,
   r_overBrightBits) applied by the last pass, as a PC's hardware gamma
   table would; without it the textures are brightened when loaded, as with
   MiniGL. Both need vid_restart. */
static cvar_t *r_vkmsaa;
static cvar_t *r_vkgamma;

static int glctx;
static int libopen;

/* Blank 16x1 sprite for hiding the pointer, in chip RAM. */
static UWORD *blankpointer;

static void GLimp_HidePointer(void)
{
	if (amiga_inputwindow && blankpointer)
		SetPointer(amiga_inputwindow, blankpointer, 1, 16, 0, 0);

	mousevisible = 0;
}

static void GLimp_InitExtensions(void)
{
	qglMultiTexCoord2fARB = 0;
	qglActiveTextureARB = 0;
	qglClientActiveTextureARB = 0;
	qglLockArraysEXT = 0;
	qglUnlockArraysEXT = 0;
	qglPNTrianglesiATI = 0;
	qglPNTrianglesfATI = 0;

	glConfig.deviceSupportsGamma = r_vkgamma->integer ? qtrue : qfalse;
	glConfig.textureEnvAddAvailable = qfalse;
	glConfig.textureCompression = TC_NONE;
	glConfig.anisotropicAvailable = qfalse;
	glConfig.textureFilterAnisotropicAvailable = qfalse;
	glConfig.NVFogAvailable = qfalse;

	if (!r_allowExtensions->integer)
	{
		ri.Printf(PRINT_ALL, "*** IGNORING OPENGL EXTENSIONS ***\n");
		return;
	}

	glConfig.maxActiveTextures = 2;
	if (r_ext_multitexture->integer)
	{
		qglMultiTexCoord2fARB = vk_glMultiTexCoord2fARB;
		qglActiveTextureARB = vk_glActiveTextureARB;
		qglClientActiveTextureARB = vk_glClientActiveTextureARB;
		ri.Printf(PRINT_ALL, "...using GL_ARB_multitexture (2 units, shaders)\n");

		/* GL_ADD is one line of the fragment shader here. */
		if (r_ext_texture_env_add->integer)
		{
			glConfig.textureEnvAddAvailable = qtrue;
			ri.Printf(PRINT_ALL, "...using GL_EXT_texture_env_add\n");
		}
	}
	else
	{
		ri.Printf(PRINT_ALL, "...not using GL_ARB_multitexture\n");
	}

	/* The locked range is what a draw copies to the GPU. */
	if (r_ext_compiled_vertex_array->integer)
	{
		qglLockArraysEXT = vk_glLockArraysEXT;
		qglUnlockArraysEXT = vk_glUnlockArraysEXT;
		ri.Printf(PRINT_ALL, "...using GL_EXT_compiled_vertex_array\n");
	}
}

static qboolean GLimp_CreateContext(qboolean fullscreen, int width, int height)
{
	struct Window *inputWindow = NULL;
	int drawWidth = 0, drawHeight = 0;

	ri.Printf(PRINT_ALL, "...creating %dx%d %s Vulkan context\n", width, height,
		fullscreen ? "fullscreen" : "windowed");

	if (!VKGL_CreateContext(width, height, fullscreen ? 1 : 0, r_swapInterval->integer ? 1 : 0,
		&inputWindow, &drawWidth, &drawHeight))
	{
		ri.Printf(PRINT_ALL, "...failed: %s\n", VKGL_ErrorText());
		return qfalse;
	}

	amiga_inputwindow = inputWindow;
	if (amiga_inputwindow)
		amiga_inputwindow->Flags |= WFLG_RMBTRAP;

	/* the renderer draws at the surface's size */
	if (drawWidth != width || drawHeight != height)
	{
		ri.Printf(PRINT_ALL, "...drawing at %dx%d, the server scales it up to %dx%d\n",
			drawWidth, drawHeight, width, height);
	}
	glConfig.vidWidth = drawWidth;
	glConfig.vidHeight = drawHeight;

	glConfig.colorBits = 32;
	glConfig.depthBits = 24;
	glConfig.stencilBits = 0;
	glConfig.isFullscreen = fullscreen;
	return qtrue;
}

void GLimp_Init(void)
{
	qboolean fullscreen;
	int width, height;

	ri.Printf(PRINT_ALL, "Initializing vulkan.library display\n");

	r_amigaspeeds = ri.Cvar_Get("r_amigaspeeds", "0", 0);
	r_vkmsaa = ri.Cvar_Get("r_vkmsaa", "4", CVAR_ARCHIVE | CVAR_LATCH);
	r_vkgamma = ri.Cvar_Get("r_vkgamma", "1", CVAR_ARCHIVE | CVAR_LATCH);
	VKGL_SetOptions(r_vkmsaa->integer);

	if (!R_GetModeInfo(&glConfig.vidWidth, &glConfig.vidHeight, &glConfig.windowAspect, r_mode->integer))
	{
		glConfig.vidWidth = 640;
		glConfig.vidHeight = 480;
		glConfig.windowAspect = 1.0f;
	}
	width = glConfig.vidWidth;
	height = glConfig.vidHeight;

	if (!VKGL_Open())
	{
		Sys_Error("Unable to open vulkan.library\n");
		return;
	}
	libopen = 1;

	fullscreen = r_fullscreen->integer ? qtrue : qfalse;
	if (!GLimp_CreateContext(fullscreen, width, height))
	{
		ri.Printf(PRINT_ALL, "...trying the other display mode\n");
		fullscreen = fullscreen ? qfalse : qtrue;
		if (!GLimp_CreateContext(fullscreen, width, height))
		{
			GLimp_Shutdown();
			Sys_Error("Unable to create a Vulkan context for %dx%d\n", width, height);
			return;
		}
	}
	glctx = 1;

	ri.Cvar_Set("r_fullscreen", fullscreen ? "1" : "0");
	r_fullscreen->modified = qfalse;

	ri.Printf(PRINT_ALL, "...device: %s\n", VKGL_DeviceName());
	ri.Printf(PRINT_ALL, "...%dx multisampling, gamma %s\n", VKGL_Samples(),
		r_vkgamma->integer ? "in the last pass" : "in the textures");

	/* No display lock here, unlike MiniGL: console output can go out as it
	   comes. */
	amiga_holdconsole = 0;

	blankpointer = AllocVec(6 * sizeof(UWORD), MEMF_CHIP | MEMF_CLEAR);
	GLimp_HidePointer();

	glConfig.driverType = GLDRV_ICD;
	glConfig.hardwareType = GLHW_GENERIC;

	Q_strncpyz(glConfig.vendor_string, (const char *)vk_glGetString(GL_VENDOR), sizeof(glConfig.vendor_string));
	Q_strncpyz(glConfig.renderer_string, (const char *)vk_glGetString(GL_RENDERER), sizeof(glConfig.renderer_string));
	Q_strncpyz(glConfig.version_string, (const char *)vk_glGetString(GL_VERSION), sizeof(glConfig.version_string));
	Q_strncpyz(glConfig.extensions_string, (const char *)vk_glGetString(GL_EXTENSIONS), sizeof(glConfig.extensions_string));

	glConfig.maxTextureSize = 0;
	vk_glGetIntegerv(GL_MAX_TEXTURE_SIZE, &glConfig.maxTextureSize);
	if (glConfig.maxTextureSize <= 0)
		glConfig.maxTextureSize = 2048;

	GLimp_InitExtensions();
}

void GLimp_Shutdown(void)
{
	Amiga_Trace("GLimp_Shutdown");
	if (glctx)
	{
		amiga_inputwindow = 0;
		Amiga_Trace("VKGL_DestroyContext");
		VKGL_DestroyContext();
		glctx = 0;
		amiga_holdconsole = 0;
		Amiga_FlushConsole();
	}

	if (blankpointer)
	{
		FreeVec(blankpointer);
		blankpointer = 0;
	}

	if (libopen)
	{
		VKGL_Close();
		libopen = 0;
	}

	memset(&glConfig, 0, sizeof(glConfig));
	memset(&glState, 0, sizeof(glState));
}

void GLimp_LogComment(char *comment)
{
}

void GLimp_EndFrame(void)
{
	unsigned int start, draws, vertexes, uploads, pipelines;

	if (!amiga_speeds)
	{
		VKGL_SwapBuffers();
		amiga_speeds = r_amigaspeeds->integer;
		return;
	}

	start = Amiga_Micros();
	VKGL_SwapBuffers();
	start = Amiga_Micros() - start;

	VKGL_Stats(&draws, &vertexes, &uploads, &pipelines);
	{
		unsigned int shade = amiga_shadeMicros > amiga_drawMicros ? amiga_shadeMicros - amiga_drawMicros : 0;

		ri.Printf(PRINT_ALL, "amiga: draw %u.%u (%u calls, %u idx, max %u verts) shade %u.%u swap %u.%u sound %u.%u"
			" | vk draws %u vertexes %u uploads %u pipelines %u\n",
			amiga_drawMicros / 1000, amiga_drawMicros / 100 % 10, amiga_drawCalls, amiga_drawIndexes,
			amiga_drawMaxVerts, shade / 1000, shade / 100 % 10, start / 1000, start / 100 % 10,
			amiga_soundMicros / 1000, amiga_soundMicros / 100 % 10, draws, vertexes, uploads, pipelines);
	}

	amiga_drawMicros = amiga_drawCalls = amiga_drawIndexes = amiga_shadeMicros = amiga_soundMicros = 0;
	amiga_drawMaxVerts = amiga_drawMaxIndexes = amiga_drawOverVerts = 0;
	{
		int i;

		for (i = 0; i < SF_NUM_SURFACE_TYPES; i++)
			amiga_surfMicros[i] = amiga_surfCount[i] = 0;
	}
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
	if (r_vkgamma && r_vkgamma->integer)
		VKGL_SetGamma(red, green, blue);
}

void GLimp_Frame(void)
{
}
