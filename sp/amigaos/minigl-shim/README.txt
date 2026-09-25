Replacement headers searched before the PiStorm3D SDK's include directory
(see OS_CFLAGS/GAME_CFLAGS in ../Makefile). They let an application build
against the shared minigl.library API (<proto/minigl.h>) with the SDK as
distributed on 2026-09-24:

mgl/context.h, mgl/vertexbuffer.h
    The SDK versions describe the library's internal state and include
    backend headers that the SDK does not ship
    (../../../backend/include/v3d_*.h). An application only holds a
    GLcontext pointer, so these copies keep just the public types.

mgl/minigl.h
    The static-linking API's inline functions. <libraries/minigl_dispatch.h>
    defines USE_MGLAPI to switch that API's macros off, which makes
    mgl/gl.h include this file instead; the real one dereferences the
    context and defines a global CC macro. The shared-library API does not
    use any of it, so this copy is empty.

Remove this directory once the SDK's public headers stand on their own.
