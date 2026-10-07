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

### GL_ADD texture environment (next, after 0.3)

The renderer can draw two-stage shaders whose second stage is additive
(`blendFunc GL_ONE GL_ONE`, e.g. glow and environment layers) in one
multitexture pass with `GL_ADD`, instead of two passes. It does this only
when `glConfig.textureEnvAddAvailable` is set (`CollapseMultitexture` in
`renderer/tr_shader.c`); `amiga_glimp.c` sets it to false.

- 29.1 defines `GL_ADD` (0x0104) and its texture environment accepts it,
  but the library does not list `GL_EXT_texture_env_add` in
  `GL_EXTENSIONS`, so the engine would never find it by itself.
- Plan: in `GLimp_Init`, set `textureEnvAddAvailable` when
  `r_ext_texture_env_add` is 1 (the default) and the library is 29.1 or
  newer, and print it in the `texenv add:` line.
- Test: `r_speeds 1` before and after (shader passes per frame), fps in the
  same spot, and a visual check of additive surfaces (lights, glowing
  signs, fire): wrong brightness would show a broken `GL_ADD`.
- Expected gain: unknown, probably small. It only affects shaders of that
  shape, but each one saves a whole pass of 68k stage setup and a draw.
  Also worth telling the PiStorm3D developers so the extension gets listed.

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

### `-fomit-frame-pointer` (later)

Turning it on broke the 3D view on hardware (the "walking on walls" look of
the float-return bug). Finding why (probably a function relying on the
frame pointer or a libnix routine) would give a few percent everywhere.
`tests/mathtest.c` was not run with that build; start there.

### Small items (idea)

- The sky is the only frequent `glBegin`/`glEnd` path (`tr_sky.c`); it
  could use vertex arrays. `r_fastsky 1` made little difference, so the
  gain would be small.
- Try `r_dynamiclight 0` in a fight (it made no difference in a quiet
  scene).
- `com_maxfps 30` for steadier pacing, as an option in `amiga.cfg`.

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
  cutscenes (an early test showed only the logo until ESC).
- **next** fps at 800x600, 1280x720 and 1920x1080.
- **idea** Other minigl.library implementations (not PiStorm3D) have never
  been tried.

## Done

- 0.1: first public test release (2026-09-25).
- 0.2: clean sound with Paula (AHI buffer 93 ms → 0.75 s), 96 MB sound
  memory, pk3 RAM cache, faster mixing loop (2026-09-29).
- Loading 70 s → ~25 s (pk3 central directory in memory); quit no longer
  locks up the machine (no Intuition pointer calls while MiniGL is open).
