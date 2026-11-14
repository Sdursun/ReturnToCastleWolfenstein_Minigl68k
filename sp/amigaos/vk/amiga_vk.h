/*
 * The display side of the Vulkan build: what amiga_vkglimp.cpp calls in
 * amiga_vkgl.c (the only file that sees Vulkan's headers).
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#ifndef AMIGA_VK_H
#define AMIGA_VK_H

struct Window;

#ifdef __cplusplus
extern "C" {
#endif

int VKGL_Open( void );
void VKGL_Close( void );

/* RTCW: before VKGL_CreateContext, 4 asks for 4x multisampling (if the GPU
   has it; VKGL_Samples says what it got). The frame goes to the screen
   through a gamma ramp, straight until VKGL_SetGamma gives one. */
void VKGL_SetOptions( int samples );
int VKGL_Samples( void );
void VKGL_SetGamma( const unsigned char *red, const unsigned char *green, const unsigned char *blue );

/* Opens the screen (fullscreen) or the window and everything Vulkan needs.
   The size the renderer draws at comes back in drawWidth/drawHeight: the
   server may ask for a smaller picture than the screen and scale it up
   (V3DVK_UPSCALE). 0 and VKGL_ErrorText() on failure. */
int VKGL_CreateContext( int width, int height, int fullscreen, int vsync,
	struct Window **inputWindow, int *drawWidth, int *drawHeight );
const char *VKGL_ErrorText( void );
const char *VKGL_DeviceName( void );
void VKGL_DestroyContext( void );
void VKGL_SwapBuffers( void );

/* Counted since the last call: draws, vertexes, texture uploads; and the
   number of pipelines made so far. */
void VKGL_Stats( unsigned int *draws, unsigned int *vertexes, unsigned int *uploads, unsigned int *pipelines );

#ifdef __cplusplus
}
#endif

#endif
