# Notes for the MiniGL / PiStorm3D developers

These are the things we ran into while porting Return to Castle Wolfenstein
to `minigl.library` (PiStorm3D release of 2026-09-24, shared-library API,
AmigaOS 3.2, Raspberry Pi 4 PiStorm with Emu68). They are meant as friendly
feedback: PiStorm3D made this port possible, and a real game running on it
turned out to be a good test. Each point says how sure we are.

We would be glad to help: test builds, logs and small test programs can be
made on request. The port's source is in this repository; the MiniGL
calls are all in `sp/amigaos/amiga_glimp.c` and `sp/amigaos/amiga_qgl.h`.

**Update for 29.1 (2026-10-07):** the port now builds against the 29.1 SDK.
Points 2, 3 and 4 are resolved in 29.1, and `glCallList` and
`glNormalPointer` now exist (point 5). Thank you! Point 1 has not been
retested, because the port no longer makes the pointer call that locked up.
The rest of this file describes 27.6, the version the notes were written
against.

## 1. Display lock held between frames locks up the machine (confirmed)

After `mglCreateContext`, Intuition and console output from the same task
lock up the whole machine (no Guru, reset needed) until the context is
deleted:

- `ClearPointer()` on the window from `mglGetInputWindowHandle()`, called
  right before `mglDeleteContext()`, froze the machine every time. With the
  call removed, quitting works every time. We traced this step by step
  with a log written line by line to a file.
- Very likely the same: with sound disabled, quitting froze exactly at the
  first write to the Shell window (stderr) while the context was still
  open. We did not isolate this one as cleanly as the pointer call.
- Calling `mglLockMode(MGL_LOCK_AUTOMATIC)` right after creating the
  context did not change this.

Our reading is that the display lock is held between frames, not only
while a frame is being drawn, so every other
display user (Intuition's pointer, console.device) waits for a lock that
our own task holds. It would help if the lock were released after
`mglSwitchDisplay`, or if the documentation said which calls are unsafe
while a context is open. Our workaround: no pointer calls after context
creation, console output held back until the context is deleted.

## 2. SDK headers include files that are not shipped (confirmed; fixed in 29.1)

`mgl/context.h` and `mgl/vertexbuffer.h` include
`../../../backend/include/v3d_*.h`, which the SDK does not contain, and
`mgl/minigl.h` (included through `mgl/gl.h` when `USE_MGLAPI` is defined
by `<libraries/minigl_dispatch.h>`) dereferences the context and defines a
global `CC` macro. An application using only `<proto/minigl.h>` therefore
cannot compile against the SDK as shipped. We replaced the three headers
with public-only versions (`sp/amigaos/minigl-shim/`, see its README). It
would be simpler for applications if the public headers stood on their
own.

## 3. Stack warning prints raw format strings and checks the wrong value (confirmed; the warning is gone in 29.1)

At startup the library prints:

```
MGLInit: WARNING -- current stack size is %lu bytes. ... recommend running 'Stack %ld' ...
```

The `%lu` / `%ld` are printed literally (the format arguments are not
applied). The check also seems to look at the Shell's stack setting
(`pr_StackSize`, 4096 here) rather than the stack the task is actually
running on: our program switches to a 2 MB stack with libnix's
`swapstack` (`tc_SPUpper - tc_SPLower` = 2097152), and the warning still
appears. Starting the program after `Stack 262144` did not change any
behaviour either.

## 4. GL_* constants are positional, not the standard values (by design, but a trap; 29.1 uses the OpenGL values)

`GL_*` enums in `mgl/gl.h` are sequential numbers, not the OpenGL values.
Code that passes numeric values (for example internal format `3` or `4` to
`glTexImage2D`, as Quake 3 based engines do, or `GL_RGBA4`) silently gets
the wrong format. We remap these in `amiga_qgl.h`. A note in the
documentation, or accepting the classic numeric internal formats in
`glTexImage2D`, would save porters some time.

## 5. Missing features we had to stub (feature requests)

- Stencil buffer (`glStencilFunc`/`Op`/`Mask`, `GL_STENCIL_BUFFER_BIT`)
- `glClipPlane`
- `GL_ADD` texture environment (`GL_EXT_texture_env_add`). 29.1 defines
  `GL_ADD` and the texture environment accepts it, but the extension is
  not listed in `GL_EXTENSIONS`; listing it would let games find it.
- `glTexParameterfv(GL_TEXTURE_BORDER_COLOR)`, `GL_NORMAL_ARRAY`,
  `glCallList(s)` (glCallList is in 29.1), `glRasterPos3fv`

None of them is needed to play the game; they would enable stencil
shadows, correct mirrors/portals and one fewer rendering pass.

## 6. Performance observations (measured)

Measured in `escape1` at 640x480, 16-bit colour, 24-bit Z, 2 buffers,
multitexture and `glLockArrays` in use:

| | per frame |
|---|---|
| `glDrawElements` | ~80–110 calls, ~25 000–31 000 indices, **2–6 ms total** |
| `mglSwitchDisplay` | **0.4–1 ms** |
| whole frame | ~24 ms (about 40 fps) |

So MiniGL is not the bottleneck; the game's own 68k renderer code is.
Two things surprised us and may be worth a look:

- **320x240 is slower than 640x480** (8–17 fps against ~40 fps), with the
  same game code. We did not investigate further.
- Three buffers (`mglChooseNumberOfBuffers(3)`) made no measurable
  difference; `mglSwitchDisplay` does not appear to wait for the display
  with two buffers either.

## 7. Things that worked well

Context creation (fullscreen and windowed), multitexture with 2 units,
`glLockArrays`/`glUnlockArrays`, texture upload (about 3.8 s for 783
textures with mipmaps), the shared-library interface and `MiniGLOpen` /
`MiniGLClose` all worked without trouble, and clean shutdown works once
the pointer calls are avoided.

## Contact

Repository: https://github.com/Sdursun/ReturnToCastleWolfenstein_Minigl68k

These notes were written with Claude Code, which developed this port
together with the repository owner.
