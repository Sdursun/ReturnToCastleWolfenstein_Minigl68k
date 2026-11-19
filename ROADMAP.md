# Roadmap

Where the AmigaOS / PiStorm3D port stands and what could come next, with
what we already know about each item. Measurements are from an A1200 with
a Raspberry Pi 4 PiStorm (Emu68), AmigaOS 3.2, 640x480, level `escape1`,
unless stated otherwise.

Status marks: **done**, **in progress**, **next**, **later**, **idea**.

## Where the time goes (what we know so far)

A frame takes about 24 ms (about 40 fps standing, 20-38 moving):

| Part | Time per frame | Notes |
|---|---|---|
| Renderer back end (`bk`) | ~13 ms | of which MiniGL draws 2-6 ms and `mglSwitchDisplay` 0.5 ms; the rest is 68k code: shader stages, tessellation |
| Client (`cl`) | ~5 ms | cgame and sound; spikes of 13-15 ms regularly and 45-51 ms in fights |
| Game logic (`gm`) | 0-5 ms | runs at 20 Hz (`sv_fps 20`), so every other frame |
| Front end (`rf`) | 1-3 ms | visibility |

MiniGL is not the bottleneck; the game's own 68k code is. Lower
resolutions do not help (320x240 was slower than 640x480).

A level load takes about 20-25 s (escape1, FFS). After the pk3 RAM cache,
the remaining big parts are JPEG/TGA decoding (~5 s), texture upload to
MiniGL (~3-4 s) and parsing the menus (~5 s, twice per map).

---

## 0.3 – PiStorm3D 29.1 (in progress)

minigl.library 29.x refuses programs built against older SDKs (dispatch
ABI 5, checked for equality; GL tokens now carry OpenGL's values). 0.1 and
0.2 were built against 27.6 and do not open on 29.x.

- **done** Build against the 29.1 SDK (`PiStorm3D/SDK`), drop
  `minigl-shim`, guard the constants in `amiga_qgl.h`.
- **next** Test on hardware with the 29.1 library: textures and colours
  (the GL token values changed), menus and fonts, fps, clean quit.
- **next** Release 0.3 with a clear "needs minigl.library 29.1" note.
- **done, ships with 0.3** `r_mode 13` (1280x720) and `r_mode 14`
  (1920x1080). Not measured yet.

## Performance

### GL_ADD texture environment (stays off: broken on PiStorm3D 29.3)

`tests/gladdtest.c` shows it. With two textures, PiStorm3D 29.3 multiplies when
asked for `GL_ADD` (centre 130 52 19, corner 0 0 0, where adding gives 255 255 160
and 130 60 40), on every draw path. It also ignores the vertex colour with two
textures; one texture is correct. QuarkTex in WinUAE does both correctly.

`textureEnvAddAvailable` therefore stays false. There would be little to gain
anyway: in `escape1`, `village1` and `dam`, `shaderlist` showed no `MT(a)` shader
with it on (144-177 `MT(m)`), probably because lightmapped shaders collapse their
first two stages with `GL_MODULATE` and only one collapse is done per shader. All
the `MT(m)` collapses in those maps use `rgbGen identity`, so the vertex colour
problem does not show there. Reported in `MINIGL_NOTES.md`.

### Vertex limit per draw (done, verified on the A1200)

MiniGL has `MGL_MAXVERTS` 1024, while the engine gathers up to 4000 vertexes per
draw. Console text alone reached 2440 vertexes in one draw, and on PiStorm3D 29.3
**all of it disappeared**. Batches now flush at `TESS_MAX_VERTEXES` (1000,
`tr_local.h`): the console text is back, and the largest draw is 996 vertexes, at no
measurable cost. `r_amigaspeeds 1` prints the largest draw of each frame and the
number above the limit. A single surface with more vertexes than the limit is still
sent whole.

Open: the logo in the console background does not show in the area under the text.
Cause not found, cosmetic.

### Timer (done, verified on the A1200 and in WinUAE)

`amiga_time.c` reads Emu68's ARM counter when `devicetree.resource` is there, and
calls `timer.device` directly everywhere else. `tests/timetest.c` checks it on any
machine. On the A1200 a read went from 20.4 µs (`gettimeofday`) to 2.0 µs, and
`escape1` at 640x480 from 24.39 to 23.75 ms a frame.

### Newer compiler (done, to be played)

The port now builds with gcc 6.5.0b 20260819 (`sacredbanana/amiga-compiler`) and
`-m68040 -m68881`: 22.04 ms a frame in `escape1`, against 23.75 ms with the older
compiler. `tests/mathtest.c` is clean, and three maps look the same and survive a
walking script. A long play, and saving and loading, are still to be tried. Why it is
faster is not known.

### Sampling profiler (next)

The timers we used (`r_amigaspeeds`) cost about 20 µs per call and
distort the result when used per surface. A profiler that samples the
program counter from a timer interrupt a few hundred times a second and
maps it to functions with `rtcw-sp.db` would show exactly which functions
cost time, in game and while loading, at almost no cost. Every other
performance item should start from its numbers.

### 68k shader and tessellation work (later)

`r_vertexlight 1` gains about 10 fps by dropping the lightmap stage, which
points at per-stage work: texture coordinate and colour generation, fog,
deforms (`tr_shade_calc.c`, `tr_shade.c`, `tr_surface.c`). Targets come
from the profiler. Each change needs a visual check.

### Periodic client spikes (later)

`cl` jumps to 13-15 ms every ~28 frames and to 45-51 ms in fights. 96 MB
of sound memory made fights smoother, so sounds being reloaded from disk
were part of it; the rest is unexplained. The profiler should show it.

### Sound mixing (measured, needs data)

The mixing loop was simplified in 0.2. `r_amigaspeeds 1` prints
`sound x.x` per frame; an A/B of `sndspeed 22050` against `11025` is still
to be done. If sound costs under 1-2 ms, leave it.

### `-fomit-frame-pointer` (tried, not worth it)

It broke the 3D view on hardware in September 2026 (the "walking on walls" look of
the float-return bug), with the older compiler. Tried again on 2026-11-04 with gcc 6.5.0b
20260819 on the A1200: `tests/mathtest.c` clean, the same start frame, the same position
after a scripted walk in `escape1`. But no gain: 22.07 ms a frame with it, 21.82 ms
without, three rounds each. It stays off. Why it broke before was not investigated.

### Small items (idea)

- The sky is the only frequent `glBegin`/`glEnd` path (`tr_sky.c`); it
  could use vertex arrays. `r_fastsky 1` made little difference, so the
  gain would be small.
- Try `r_dynamiclight 0` in a fight (it made no difference in a quiet
  scene).
- `com_maxfps 30` for steadier pacing, as an option in `amiga.cfg`.

### A GLES2 renderer on ogles2.library (idea, long term)

[ogles2](https://github.com/ladislawv/ogles2) (`ogles2.library` 1.3.2, by
R-Typer and Ladislawv) is OpenGL ES 2.0 for AmigaOS 3.x on a Pi 4
PiStorm, built on PiStorm3D's V3D backend. Unlike MiniGL it runs GLSL
shaders on the GPU, and it has buffer objects (VBOs), stencil, framebuffer
objects, DXT textures and 4x MSAA. Its own documentation says what our
measurements say: the GPU is fast, the 68k side is the limit.

Why it fits our bottleneck: most of the ~13 ms back end is 68k work per
vertex and per shader stage. A GLES2 renderer could move it to the GPU:

- `tcMod`, `rgbGen`/`alphaGen` waves, `deformVertexes` and fog computed
  in vertex and fragment shaders instead of on the 68k;
- static world geometry uploaded once into VBOs instead of tessellated
  every frame;
- multi-stage shaders blended in one pass;
- features MiniGL lacks: stencil shadows, clipping for mirrors and
  portals (in the shader), real gamma.

What it costs and what is not known yet:

- A new renderer back end that turns Quake 3 shaders into GLSL: weeks of
  work, not a port. iortcw's OpenGL 2 renderer (rend2) is a possible
  starting point, but it targets desktop GL and would need a GLES2 and
  68k-friendly rework.
- gl4es on top of ogles2 (fixed-function GL translated to GLES2) is not a
  shortcut: it adds a translation layer on the 68k instead of removing
  work, and it generates shaders at run time, so it needs the 19 MB
  `v3dglslc` compiler installed. Probably slower than MiniGL.
- The SDK is documented for clib2 (`-mcrt=clib2`); this port uses libnix.
  Whether `libogles2.a` links and works with libnix has to be tried.
- Shaders should ship precompiled (`.v3dp` and `.v3ds` files, named by a
  hash of the GLSL text); otherwise the first start compiles for seconds
  and needs the compiler. Windowed mode needs a 32-bit RTG screen.

First steps, if this is taken up:

1. A small test program linked against `libogles2.a` with libnix and
   `-noixemul`, to settle the C library question.
2. Use the sampling profiler's numbers to estimate how much of a frame a
   GLES2 renderer would remove.
3. Prototype: world surfaces only, in VBOs with a generic lightmapped
   shader, next to the MiniGL renderer, and compare fps in `escape1`.

## Loading

- **done** pk3 central directory kept in memory (70 → 25 s).
- **done** pk3 RAM cache (`fs_ramcache`): later loads 23 → 18 s.
- **next** Cache decoded textures in the same way, so later loads skip
  JPEG decoding (~5 s) and part of the texture preparation. Upload to
  MiniGL stays, as every renderer start needs it.
- **later** Menu parsing (~5 s, twice per map). Keeping the UI module
  loaded across maps is risky (about 100 places store renderer handles,
  many registered lazily); measuring why parsing is slow is the better
  first step (allocator, `l_script.c` at `-O1`, `menudef.h` parsed again
  for every menu file).
- **idea** Remember files that were not found: about 3,900 failed lookups
  per map cost 1.5-2 s.

## Usability

- **idea** Workbench icon with ToolTypes, so the game starts without a
  Shell.
- **idea** Mouse pointer in windowed mode: Intuition pointer calls locked
  up the machine while a MiniGL 27.6 context was open; retest with 29.1.
- **idea** Turkish translation as a data pk3. The fonts are pre-rendered
  images and probably lack ğ ş ı İ, so that has to be checked first.

## Testing

- **next** Play through more levels, saving and loading games (the reset
  of the statically linked game modules is exercised there), and the
  cutscenes. (The "only the logo until ESC" seen with `+map` was the intro
  video started over the map; fixed 2026-11-19.)
- **next** fps at 800x600, 1280x720 and 1920x1080.
- **idea** Other minigl.library implementations (not PiStorm3D) have never
  been tried.

## Done

- 0.1: first public test release (2026-09-25).
- 0.2: clean sound with Paula (AHI buffer 93 ms → 0.75 s), 96 MB sound
  memory, pk3 RAM cache, faster mixing loop (2026-09-29).
- Loading 70 s → ~25 s (pk3 central directory in memory); quit no longer
  locks up the machine (no Intuition pointer calls while MiniGL is open).
