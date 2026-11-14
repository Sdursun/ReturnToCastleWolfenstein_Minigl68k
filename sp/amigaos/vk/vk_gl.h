/*
 * The OpenGL 1.x the renderer is written for, on top of vulkan.library:
 * the Vulkan build only. An experiment, taken over from the Jedi Outcast port
 * (jk2sp_vk) and extended with GL fog for RTCW.
 *
 * The renderer calls qglFoo, which qgl_linked.h turns into glFoo, which this
 * header turns into vk_glFoo: the fixed function emulation in
 * amiga_vkgl.c. Mesa's GL/gl.h (in this directory) gives the desktop
 * types, constants and, through these macros, the prototypes of the
 * vk_ functions. Only amiga_vkgl.c sees Vulkan's headers.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#ifndef VK_GL_H
#define VK_GL_H

#define glActiveTextureARB          vk_glActiveTextureARB
#define glAlphaFunc                 vk_glAlphaFunc
#define glArrayElement              vk_glArrayElement
#define glBegin                     vk_glBegin
#define glBindTexture               vk_glBindTexture
#define glBlendFunc                 vk_glBlendFunc
#define glCallList                  vk_glCallList
#define glCallLists                 vk_glCallLists
#define glRasterPos3fv              vk_glRasterPos3fv
#define glClear                     vk_glClear
#define glClearColor                vk_glClearColor
#define glClearDepth                vk_glClearDepth
#define glClearStencil              vk_glClearStencil
#define glClientActiveTextureARB    vk_glClientActiveTextureARB
#define glClipPlane                 vk_glClipPlane
#define glColor3f                   vk_glColor3f
#define glColor3fv                  vk_glColor3fv
#define glColor4f                   vk_glColor4f
#define glColor4ub                  vk_glColor4ub
#define glColor4ubv                 vk_glColor4ubv
#define glColorMask                 vk_glColorMask
#define glColorPointer              vk_glColorPointer
#define glCullFace                  vk_glCullFace
#define glDeleteTextures            vk_glDeleteTextures
#define glDepthFunc                 vk_glDepthFunc
#define glDepthMask                 vk_glDepthMask
#define glDepthRange                vk_glDepthRange
#define glDisable                   vk_glDisable
#define glDisableClientState        vk_glDisableClientState
#define glDrawArrays                vk_glDrawArrays
#define glDrawBuffer                vk_glDrawBuffer
#define glDrawElements              vk_glDrawElements
#define glEnable                    vk_glEnable
#define glEnableClientState         vk_glEnableClientState
#define glEnd                       vk_glEnd
#define glFinish                    vk_glFinish
#define glFogf                      vk_glFogf
#define glFogfv                     vk_glFogfv
#define glFogi                      vk_glFogi
#define glFrustum                   vk_glFrustum
#define glGetDoublev                vk_glGetDoublev
#define glGetError                  vk_glGetError
#define glGetFloatv                 vk_glGetFloatv
#define glHint                      vk_glHint
#define glGetIntegerv               vk_glGetIntegerv
#define glGetString                 vk_glGetString
#define glLineWidth                 vk_glLineWidth
#define glLoadIdentity              vk_glLoadIdentity
#define glLoadMatrixf               vk_glLoadMatrixf
#define glMatrixMode                vk_glMatrixMode
#define glMultiTexCoord2fARB        vk_glMultiTexCoord2fARB
#define glNormal3fv                 vk_glNormal3fv
#define glNormalPointer             vk_glNormalPointer
#define glOrtho                     vk_glOrtho
#define glPointSize                 vk_glPointSize
#define glPolygonMode               vk_glPolygonMode
#define glPolygonOffset             vk_glPolygonOffset
#define glPopMatrix                 vk_glPopMatrix
#define glPushMatrix                vk_glPushMatrix
#define glReadPixels                vk_glReadPixels
#define glRotatef                   vk_glRotatef
#define glScissor                   vk_glScissor
#define glShadeModel                vk_glShadeModel
#define glStencilFunc               vk_glStencilFunc
#define glStencilMask               vk_glStencilMask
#define glStencilOp                 vk_glStencilOp
#define glTexCoord2f                vk_glTexCoord2f
#define glTexCoord2fv               vk_glTexCoord2fv
#define glTexCoordPointer           vk_glTexCoordPointer
#define glTexEnvf                   vk_glTexEnvf
#define glTexImage2D                vk_glTexImage2D
#define glTexParameterf             vk_glTexParameterf
#define glTexParameterfv            vk_glTexParameterfv
#define glTexSubImage2D             vk_glTexSubImage2D
#define glTranslatef                vk_glTranslatef
#define glVertex2f                  vk_glVertex2f
#define glVertex3f                  vk_glVertex3f
#define glVertex3fv                 vk_glVertex3fv
#define glVertexPointer             vk_glVertexPointer
#define glViewport                  vk_glViewport

/* The renderer's own extension definitions (qgl.h) are used, not glext.h. */
#define GL_GLEXT_LEGACY
#include "GL/gl.h"

/* Not in Mesa's GL 1.x header. */
#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT       0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT   0x84FF
#endif

#ifdef __cplusplus
extern "C" {
#endif

void vk_glLockArraysEXT( GLint first, GLsizei count );
void vk_glUnlockArraysEXT( void );

#ifdef __cplusplus
}
#endif

#endif
