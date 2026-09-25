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
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <intuition/extensions.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/cybergraphics.h>

#include <tgl/gl.h>
#include <tgl/gla.h>

#include <proto/tinygl.h>

#include "../renderer/tr_local.h"
#include "../client/client.h"

#include "morphos_glimp.h"

int gl_NormalFontBase = 0;

#ifndef SA_GammaControl
#define SA_GammaControl (SA_Dummy + 123)
#endif

#ifndef SA_3DSupport
#define SA_3DSupport (SA_Dummy + 127)
#endif

#ifndef SA_GammaRed
#define SA_GammaRed (SA_Dummy + 124)
#endif

#ifndef SA_GammaBlue
#define SA_GammaBlue (SA_Dummy + 125)
#endif

#ifndef SA_GammaGreen
#define SA_GammaGreen (SA_Dummy + 126)
#endif

struct Window *window;
struct Screen *screen;

static void *pointermem;

struct Library *TinyGLBase;
GLContext *__tglContext;
static int glctx = 0;

static unsigned char *gammatable;

static const ULONG black0[] = { 1 << 16, 0, 0, 0, 0 };

int mousevisible;

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

static void stub_glLockArraysEXT(int a, int b)
{
	glLockArraysEXT(a, b);
}

static void stub_glUnlockArraysEXT()
{
	glUnlockArraysEXT();
}

void GLimp_Init(void)
{
	char buf[1024];
	unsigned int modeid = INVALID_ID;
	int depth;
	int doclone;

	struct TagItem tgltags[] =
	{
		{ 0, 0 },
		{ TGL_CONTEXT_STENCIL, TRUE },
		{ TAG_DONE }
	};

	int r;

	if (!r_stencilbits->value)
	{
		tgltags[1].ti_Tag = TAG_IGNORE;
	}

	if (r_mode->integer == -2)
		doclone = 1;
	else
	{
		if ( !R_GetModeInfo( &glConfig.vidWidth, &glConfig.vidHeight, &glConfig.windowAspect, r_mode->integer ) )
		{
			Sys_Error("Unable to get mode info\n");
			return;
		}

		doclone = 0;
	}

	TinyGLBase = OpenLibrary("tinygl.library", 0);
	if (TinyGLBase)
	{
		if (TinyGLBase->lib_Version > 50 || (TinyGLBase->lib_Version == 50 && TinyGLBase->lib_Revision >= 9))
		{
			if (r_fullscreen->integer)
			{
				depth = r_colorbits->integer;

				if (depth == 32)
					depth = 24;

				if (depth != 15 && depth != 16 && depth != 24)
					depth = 16;

				if (!(IntuitionBase->LibNode.lib_Version > 50 || (IntuitionBase->LibNode.lib_Version == 50 && IntuitionBase->LibNode.lib_Revision >= 50)))
				{
					modeid = BestCModeIDTags(
						CYBRBIDTG_Depth, depth,
						CYBRBIDTG_NominalWidth, glConfig.vidWidth,
						CYBRBIDTG_NominalHeight, glConfig.vidHeight,
						TAG_DONE);

					if (modeid == INVALID_ID)
					{
						ri.Printf(PRINT_ALL, "Unable to find a screen mode\n");
					}
				}

				if (modeid != INVALID_ID || (IntuitionBase->LibNode.lib_Version > 50 || (IntuitionBase->LibNode.lib_Version == 50 && IntuitionBase->LibNode.lib_Revision >= 50)))
				{
					screen = OpenScreenTags(0,
						doclone?TAG_IGNORE:SA_Width, glConfig.vidWidth,
						doclone?TAG_IGNORE:SA_Height, glConfig.vidHeight,
						doclone?TAG_IGNORE:SA_Depth, depth,
						SA_Quiet, TRUE,
						SA_GammaControl, TRUE,
						SA_3DSupport, TRUE,
						SA_Colors32, black0,
						modeid!=INVALID_ID?SA_DisplayID:TAG_IGNORE, modeid,
						TAG_DONE);

				}
			}

			if (doclone)
			{
				if (screen)
				{
					glConfig.vidWidth = screen->Width;
					glConfig.vidHeight = screen->Height;
				}
				else
				{
					glConfig.vidWidth = 640;
					glConfig.vidHeight = 480;
				}

				glConfig.windowAspect = 1;
			}

			if (screen)
			{
				if (IntuitionBase->LibNode.lib_Version > 50 || (IntuitionBase->LibNode.lib_Version == 50 && IntuitionBase->LibNode.lib_Revision >= 74))
				{
					gammatable = AllocVec(256*3, MEMF_ANY);
					if (gammatable)
						glConfig.deviceSupportsGamma = qtrue;
				}
			}

			window = OpenWindowTags(0,
				WA_InnerWidth, glConfig.vidWidth,
				WA_InnerHeight, glConfig.vidHeight,
				WA_Title, "Return to Castle Wolfenstein",
				WA_DragBar, screen?FALSE:TRUE,
				WA_DepthGadget, screen?FALSE:TRUE,
				WA_Borderless, screen?TRUE:FALSE,
				WA_RMBTrap, TRUE,
				screen?WA_PubScreen:TAG_IGNORE, (ULONG)screen,
				WA_Activate, TRUE,
				TAG_DONE);

			if (window)
			{
				__tglContext = GLInit();
				if (__tglContext)
				{
					if (screen)
					{
						tgltags[0].ti_Tag = TGL_CONTEXT_SCREEN;
						tgltags[0].ti_Data = (ULONG)screen;
					}
					else
					{
						tgltags[0].ti_Tag = TGL_CONTEXT_WINDOW;
						tgltags[0].ti_Data = (ULONG)window;
					}

					r = GLAInitializeContext(__tglContext, tgltags);

					if (r)
					{
						glctx = 1;

						pointermem = AllocVec(256, MEMF_ANY|MEMF_CLEAR);
						if (pointermem)
						{
							SetPointer(window, pointermem, 16, 16, 0, 0);

							glConfig.driverType = GLDRV_ICD;
							glConfig.hardwareType = GLHW_GENERIC;

							// get our config strings
							Q_strncpyz( glConfig.vendor_string, qglGetString (GL_VENDOR), sizeof( glConfig.vendor_string ) );
							Q_strncpyz( glConfig.renderer_string, qglGetString (GL_RENDERER), sizeof( glConfig.renderer_string ) );
							if (*glConfig.renderer_string && glConfig.renderer_string[strlen(glConfig.renderer_string) - 1] == '\n')
								glConfig.renderer_string[strlen(glConfig.renderer_string) - 1] = 0;
							Q_strncpyz( glConfig.version_string, qglGetString (GL_VERSION), sizeof( glConfig.version_string ) );
							Q_strncpyz( glConfig.extensions_string, qglGetString (GL_EXTENSIONS), sizeof( glConfig.extensions_string ) );
	
							if ( strstr( glConfig.extensions_string, "GL_ARB_multitexture" ) )
							{
							        qglGetIntegerv( GL_MAX_ACTIVE_TEXTURES_ARB, &glConfig.maxActiveTextures );
							        if ( glConfig.maxActiveTextures > 1 )
								{
									qglMultiTexCoord2fARB = stub_glMultiTexCoord2fARB;
									qglActiveTextureARB = stub_glActiveTextureARB;
									qglClientActiveTextureARB = stub_glClientActiveTextureARB;
								}
							}
							if ( strstr( glConfig.extensions_string, "EXT_texture_env_add" ) )
							{
								if ( r_ext_texture_env_add->integer )
								{
									glConfig.textureEnvAddAvailable = qtrue;
								}
							}

							if ( strstr( glConfig.extensions_string, "GL_EXT_compiled_vertex_array" ) )
							{
								if ( r_ext_compiled_vertex_array->value )
								{
									qglLockArraysEXT = stub_glLockArraysEXT;
									qglUnlockArraysEXT = stub_glUnlockArraysEXT;
								}
							}

							glConfig.colorBits = GetBitMapAttr(window->WScreen->RastPort.BitMap, BMA_DEPTH);
							glGetIntegerv(GL_STENCIL_BITS, &r);
							glConfig.stencilBits = r;
							if (r)
								glConfig.depthBits = 24;
							else
								glConfig.depthBits = 16;

							if (screen)
							{
								/* Because my driver sucks :) */

								FillPixelArray(window->RPort, 0, 0, glConfig.vidWidth, glConfig.vidHeight, 0);
							}

							return;
						}
						else
						{
							ri.Printf(PRINT_ALL, "Unable to allocate memory for mouse pointer\n");
						}

						if (screen)
						{
							glADestroyContextScreen();
						}
						else
						{
							glADestroyContextWindowed();
						}

						glctx = 0;
					}
					else
					{
						ri.Printf(PRINT_ALL, "Unable to initialize GL context\n");
					}

					GLClose(__tglContext);
					__tglContext = 0;
				}
				else
				{
					ri.Printf(PRINT_ALL, "Unable to create GL context\n");
				}

				CloseWindow(window);
				window = 0;

				if (screen)
				{
					CloseScreen(screen);
					screen = 0;
				}
			}
			else
			{
				ri.Printf(PRINT_ALL, "Unable to open window\n");
			}

		}
		else
		{
			ri.Printf(PRINT_ALL, "TinyGL version 50.9 required\n");
		}

		CloseLibrary(TinyGLBase);
		TinyGLBase = 0;
	}
	else
	{
		ri.Printf(PRINT_ALL, "Couldn't open tinygl.library");
	}

	Sys_Error("Unable to open a display\n");
}

void GLimp_Shutdown(void)
{
	if (glctx)
	{
		if (screen && !(TinyGLBase->lib_Version == 0 && TinyGLBase->lib_Revision < 4))
		{
			glADestroyContextScreen();
		}
		else
		{
			glADestroyContextWindowed();
		}

		glctx = 0;
	}

	if (__tglContext)
	{
		GLClose(__tglContext);
		__tglContext = 0;
	}

	if (window)
	{
		CloseWindow(window);
		window = 0;
	}

	if (pointermem)
	{
		FreeVec(pointermem);
		pointermem = 0;
	}

	if (screen)
	{
		CloseScreen(screen);
		screen = 0;
	}

	if (TinyGLBase)
	{
		CloseLibrary(TinyGLBase);
		TinyGLBase = 0;
	}

	if (gammatable)
	{
		FreeVec(gammatable);
		gammatable = 0;
	}

	memset( &glConfig, 0, sizeof( glConfig ) );
	memset( &glState, 0, sizeof( glState ) );
}

void GLimp_LogComment(char *comment)
{
}

void GLimp_EndFrame(void)
{
	glASwapBuffers();
}

void *GLimp_RendererSleep(void)
{
}

qboolean GLimp_SpawnRenderThread(void (*function)(void))
{
	return 0;
}

void GLimp_FrontEndSleep(void)
{
}

void GLimp_WakeRenderer(void *data)
{
}

void GLimp_SetGamma(unsigned char red[256], unsigned char green[256], unsigned char blue[256])
{
	if (glConfig.deviceSupportsGamma)
	{
		memcpy(gammatable, red, 256);
		memcpy(gammatable+256, green, 256);
		memcpy(gammatable+512, blue, 256);

		SetAttrs(screen,
			SA_GammaRed, gammatable,
			SA_GammaGreen, gammatable+256,
			SA_GammaBlue, gammatable+512,
			TAG_DONE);
	}
}

void GLimp_Frame()
{
	if (screen == 0)
	{
		if ((cls.keyCatchers & KEYCATCH_CONSOLE))
		{
			if (!mousevisible)
			{
				ClearPointer(window);
				mousevisible = 1;
			}
		}
		else
		{
			if (mousevisible)
			{
				SetPointer(window, pointermem, 16, 16, 0, 0);

				mousevisible = 0;
			}
		}
	}
}
