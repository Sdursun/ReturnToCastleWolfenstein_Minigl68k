/*
 * AmigaOS 3.x / MiniGL (PiStorm3D) compatibility layer for the renderer.
 *
 * Included from renderer/qgl.h after qgl_linked.h. It fills the gaps
 * between what the RTCW renderer calls and what minigl.library offers.
 *
 * Written for the PiStorm3D 29.1 SDK, whose GL_* constants carry OpenGL's
 * own values. Constants the SDK does not define are added here with their
 * OpenGL values too, so nothing collides with a value MiniGL acts on.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#ifndef AMIGA_QGL_H
#define AMIGA_QGL_H

/* ---- constants the SDK may not define ---------------------------------- */

/* texture_env_add: the SDK defines GL_ADD, but the library does not list
   the extension; glimp leaves textureEnvAddAvailable false for now. */
#ifndef GL_ADD
#define GL_ADD                          0x0104
#endif

/* No stencil buffer. glimp reports stencilBits = 0, which makes the
   renderer skip stencil shadows (r_shadows 2) and r_measureOverdraw. */
#ifndef GL_STENCIL_TEST
#define GL_STENCIL_TEST                 0x0B90
#endif
#ifndef GL_STENCIL_INDEX
#define GL_STENCIL_INDEX                0x1901
#endif
#ifndef GL_KEEP
#define GL_KEEP                         0x1E00
#endif
#ifndef GL_INCR
#define GL_INCR                         0x1E02
#endif
#ifndef GL_DECR
#define GL_DECR                         0x1E03
#endif
/* Used in glClear masks: with no stencil buffer it must not set a bit, so
   it is 0 whatever the SDK says. */
#undef GL_STENCIL_BUFFER_BIT
#define GL_STENCIL_BUFFER_BIT           0

/* No user clip planes: portals and mirrors render without the clip. */
#ifndef GL_CLIP_PLANE0
#define GL_CLIP_PLANE0                  0x3000
#endif

#ifndef GL_NORMAL_ARRAY
#define GL_NORMAL_ARRAY                 0x8075
#endif
#ifndef GL_TEXTURE_BORDER_COLOR
#define GL_TEXTURE_BORDER_COLOR         0x1004
#endif

/* 16-bit RGBA texture request (r_texturebits 16), remapped below. */
#ifndef GL_RGBA4
#define GL_RGBA4                        0x8056
#endif

/* Wireframe (r_showtris and friends, debugging only). */
#ifndef GL_LINE
#define GL_LINE                         0x1B01
#endif

/* ---- functions MiniGL does not provide, or the renderer must not use --- */

#undef qglStencilFunc
#undef qglStencilOp
#undef qglStencilMask
#undef qglClearStencil
#undef qglClipPlane
#undef qglCallList
#undef qglNormalPointer
#undef qglTexParameterfv
#undef qglTexImage2D
#undef qglRasterPos3fv
#undef qglCallLists

#define qglStencilFunc( func, ref, mask )       ( (void)0 )
#define qglStencilOp( fail, zfail, zpass )      ( (void)0 )
#define qglStencilMask( mask )                  ( (void)0 )
#define qglClearStencil( s )                    ( (void)0 )
#define qglClipPlane( plane, equation )         ( (void)0 )

/* 29.1 has glCallList and glNormalPointer, but the renderer only reaches
   them on paths this port does not use (display lists, normals for
   debugging), so they stay off as in the tested builds. */
#define qglCallList( list )                     ( (void)0 )
#define qglNormalPointer( type, stride, ptr )   ( (void)0 )

/* Text labels on camera spline points (splines.cpp, editor debugging). */
#define qglRasterPos3fv( v )                    ( (void)0 )
#define qglCallLists( n, type, lists )          ( (void)0 )

/* The only glTexParameterfv call is the fog image's border colour, which
   MiniGL cannot take; anything else is forwarded as a scalar. */
#define qglTexParameterfv( target, pname, params ) \
	( ( pname ) == GL_TEXTURE_BORDER_COLOR ? (void)0 : glTexParameterf( target, pname, *( params ) ) )

/* The renderer still uses the GL 1.0 internal formats 3 and 4 (component
   counts), and GL_RGBA4, which MiniGL does not take. Map them onto the
   sized formats it understands. */
static __inline__ GLint AmigaQGL_InternalFormat( GLint fmt ) {
	switch ( fmt ) {
	case 3:         return GL_RGB;
	case 4:         return GL_RGBA;
	case GL_RGBA4:  return GL_RGBA;
	default:        return fmt;
	}
}

#define qglTexImage2D( target, level, internalformat, width, height, border, format, type, pixels ) \
	glTexImage2D( target, level, AmigaQGL_InternalFormat( internalformat ), width, height, border, format, type, pixels )

#endif
