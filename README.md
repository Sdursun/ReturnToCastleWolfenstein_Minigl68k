# Return to Castle Wolfenstein – single player for AmigaOS 3.x (MiniGL, 68k)

A port of the Return to Castle Wolfenstein single player game to 68k
AmigaOS 3.x. It renders through `minigl.library` and was written for
[PiStorm3D](https://github.com/SteffenHaeuser/MiniGL_Library_68k), the
hardware MiniGL for Raspberry Pi 4 / CM4 based PiStorm systems (Emu68). It
is based on the MorphOS port of the GPL source release.

**Türkçe açıklama aşağıda / Turkish description below.**

## About this project

The code of this port was written entirely with
[Claude Code](https://claude.com/claude-code), Anthropic's AI coding
assistant, working together with me on real hardware: I ran every build on
my Amiga and sent back the logs, and Claude Code read the source, found
the problems and wrote the fixes.

I do not normally port games. This is an experimental project for me, made
to see how far this way of working can go. It is not the work of an
experienced Amiga game programmer, and there is certainly a lot left to
improve. I believe a good programmer can do wonderful things with this
code base and with PiStorm3D, and I hope someone picks it up.

## Status

Tested on AmigaOS 3.2, PiStorm with Raspberry Pi 4 (Emu68), PiStorm3D
`minigl.library` 30.0:

- Menus, levels, cutscenes, sound (AHI), keyboard, mouse and console work.
- Frame rate in the first level (`escape1`), standing, A1200 with PiStorm:

  | Mode | fps |
  |---|---:|
  | 640x480 (`r_mode 3`) | 47 |
  | 800x600 (`r_mode 4`) | 42 |
  | 1024x768 (`r_mode 6`, the default) | 36 |
  | 1280x720 (`r_mode 13`) | 33 |
  | 1920x1080 (`r_mode 14`) | 19 |

  Moving and fighting is slower; `r_vertexlight 1` adds about 10 fps.
- Loading a level took about 20-25 s from an FFS partition; 0.4 takes
  about 4 s off that. With 1 GB of RAM, files read from the pk3s are kept
  in a RAM cache, and later loads are faster again.
- Sound is clean with Paula; fights play smoother with the 96 MB sound
  memory that is now the default.
- Quitting returns cleanly to the Shell.

## Changes

**0.4.1**

- Fixed: after a few minutes of play the player froze while the world went
  on (enemies kept moving and shooting), keyboard and mouse stopped
  answering and, with sound on, the last bit of sound looped. The game's
  task priority was raised to 20 and stayed there, starving every task
  below it (input commodities, the HDMI sound driver, the network). After
  each frame the priority is now put back and MiniGL's display lock
  released.
- Faster in open areas (`dam`, 1024x768: 24.9 → 27.2 fps with all dynamic
  lights): dynamic lights skip the vertexes they cannot reach, and the
  sound is mixed at 22 kHz (RTCW's sounds are 22 kHz) instead of the HDMI
  driver's 48 kHz.
- **Dynamic lights: only the ones the game forces** (lightning and the
  like) by default, `r_dynamiclight 2`: 30.5 fps there. Torches, fire and
  muzzle flashes no longer light the walls around them. To get all of them
  back: `/r_dynamiclight 1` in the console (it is saved).
- GL_ADD (`r_ext_texture_env_add`) is on: glowing stages are drawn in one
  pass with the texture below. PiStorm3D's library multiplies there
  (`sp/amigaos/tests/gladdtest.c`), but nothing wrong was seen in play;
  `r_ext_texture_env_add 0` turns it off.
- Mouse movement no longer goes through the 32-entry input event ring,
  which could overflow and drop button releases.

**0.4**

- Tested on hardware with `minigl.library` 29.3 and 30.0.
- Faster: about 41 → 47 fps at 640x480 in `escape1`, from a newer compiler
  with FPU code (`-m68881`), a cheaper clock (Emu68's ARM counter instead
  of `gettimeofday`) and `minigl.library` 30.0.
- Level loading about 4 s shorter: lookups no longer try every file in
  directories that do not exist on the disk (10,000 tries, 4.5 s, on
  `escape1`).
- 1280x720 and 1920x1080 can be picked in the menu (Options > System >
  Video Mode).
- The default mode is 1024x768 (`r_mode 6`). `amiga.cfg` no longer sets
  `r_mode`, `r_vertexlight 1` or `r_picmip 1`: lightmaps are on, and
  textures are full size by default (`r_picmip 0`). On the A1200 that
  costs no frame rate, about 1 s of loading; `r_picmip 1` saves memory.
- Fixed: console text disappeared (draws of more than about 1,000
  vertexes vanish on PiStorm3D, they are now split).
- Fixed: starting with `+map` / `+devmap` stopped on the logo, because the
  intro video was started over the level; ESC then quit the game.
- Fixed: after a fatal error the MiniGL screen could stay open.
- Absolute pointer input (tablets, remote control tools) moves the menu
  cursor.
- A Workbench icon: a 64x64 GlowIcon with 32 colours, and a 4 colour image
  for older Workbenches. `sp/amigaos/tools/mkicon.py` draws it.
- Built with `-fbbb=-`: the compiler's extra optimizer passes made wrong
  code in a related port.

**0.3** (test release, not tested on hardware)

- Built against the PiStorm3D 29.1 SDK. **Needs minigl.library 29.1 or
  newer**; 0.1 and 0.2 need the older 27.x library, and 29.x refuses them.
- `r_mode 13` (1280x720) and `r_mode 14` (1920x1080).
- `ROADMAP.md` with the plans and what we know about each item.

**0.2**

- Fixed crackling sound, which got worse when the frame rate dropped: the
  AHI buffer was only 93 ms with the Paula 14-bit modes, while the game
  mixes half a second ahead. It is now about 0.75 s.
- Sound memory (`com_soundMegs`) is now 96 MB by default; with 24 MB fights
  stuttered. Needs about 200 MB of free Fast RAM in total.
- New RAM cache for files read from the pk3s (`fs_ramcache`), switched on
  automatically when at least 600 MB of Fast RAM are free (256 MB cache).
- A slightly faster sound mixing loop.
- `amiga.cfg`: removed a semicolon from a comment that made the console
  print `Unknown command "lower"`.
- `r_amigaspeeds 1` also shows the time spent on sound.

**0.1** – first public test release.

What comes next, with what we know about each item: [`ROADMAP.md`](ROADMAP.md).

Known limitations:

- Single player only, no networking.
- No stencil shadows, no clip planes for mirrors/portals, no gamma control
  (MiniGL does not offer them).
- The mouse pointer is not shown in windowed mode.
- Most of the frame time is spent in the renderer's 68k code (shader stage
  setup, tessellation), not in MiniGL; see `MINIGL_NOTES.md`.

## Installing and playing

You need the game data of the PC version patched to 1.41. The game data is
**not** part of this repository and must never be distributed with it.

Requirements:

- AmigaOS 3.x, a 68040 or better with an FPU (Emu68 qualifies)
- RTG (Picasso96 or CyberGraphX)
- `minigl.library` **29.1 or newer** in `LIBS:` (from the PiStorm3D release)
- AHI
- Plenty of Fast RAM (about 25 MB for the executable, as much again for
  data)

Steps:

1. Copy the `main/` directory of the PC installation (at least `pak0.pk3`,
   `sp_pak1.pk3`, `sp_pak2.pk3`, `sp_pak3.pk3`) to e.g. `Games:RTCW/main`.
2. Copy `rtcw-sp` (see *Building*) and its icon `sp/amigaos/rtcw-sp.info`
   to `Games:RTCW`, and `sp/amigaos/amiga.cfg` to `Games:RTCW/main`.
3. Double-click the icon on Workbench, or from a Shell:

   ```
   cd Games:RTCW
   rtcw-sp +exec amiga.cfg
   ```

   To start a level directly (cheats enabled): `rtcw-sp +exec amiga.cfg +devmap escape1`

Add `exec amiga.cfg` to `main/autoexec.cfg` to make the settings
permanent. More notes (sound, console, settings) are in
[`sp/amigaos/README.txt`](sp/amigaos/README.txt).

## Building

The game is cross-compiled on a PC with Stefan "Bebbo" Franke's
m68k-amigaos-gcc 6.5 toolchain and libnix. The easiest way is a Docker image
with that toolchain, which works the same on Windows, Linux and macOS.

### 1. Install Docker

- Windows / macOS: install Docker Desktop and start it.
- Linux: install `docker` from your distribution and make sure your user
  can run it (`docker run hello-world`).

Then get the toolchain image (about 6.5 GB):

```
docker pull sacredbanana/amiga-compiler:m68k-amigaos
```

The port was last built and tested with gcc 6.5.0b 20260819 from this image.
The tag moves, so if a later compiler misbehaves, run `mathtest` (below) first.

### 2. Get the source

```
git clone https://github.com/Sdursun/ReturnToCastleWolfenstein_Minigl68k.git
cd ReturnToCastleWolfenstein_Minigl68k
```

### 3. Add the PiStorm3D SDK

The MiniGL headers and link library are not in this repository. Download
the PiStorm3D release (see the
[PiStorm3D repository](https://github.com/SteffenHaeuser/MiniGL_Library_68k))
and copy its `PiStorm3D` directory next to `sp/`, so that these exist:

```
PiStorm3D/SDK/minigl-shared-library/include/proto/minigl.h
PiStorm3D/SDK/backend/include/v3d_vertex.h
PiStorm3D/SDK/lib/libminigl.a
```

This needs the **29.1 SDK or newer**. The library and its clients must
match: a program built against the 29.x SDK needs `minigl.library` 29.x,
and 29.x refuses programs built against older SDKs (0.1 and 0.2 of this
port were built against 27.6).

A different location can be given on the make command line:
`./build.sh MINIGL=/path/to/PiStorm3D/SDK` (the path as seen inside the
container, where the repository root is `/src`).

### 4. Build

On Linux or macOS, and on Windows in Git Bash:

```
cd sp/amigaos
./build.sh -j8
```

`build.sh` runs `make` inside the Docker image with the repository
mounted at `/src`, so the toolchain does not need to be installed.
Arguments are passed on to `make`. The result is

```
sp/amigaos/objects/rtcw-sp        the executable (stripped, about 3.6 MB)
sp/amigaos/objects/rtcw-sp.db     the same with symbols, for debugging
```

Other targets:

```
./build.sh mathtest     sp/amigaos/objects/mathtest, checks the math library (see below)
./build.sh clean        removes sp/amigaos/objects
```

The Makefile does not track headers: after changing one, run `./build.sh clean`
first. On Windows in PowerShell or cmd, run the same command `build.sh` runs:

```
docker run --rm -v "%CD%:/src" -w /src/sp/amigaos sacredbanana/amiga-compiler:m68k-amigaos make -j8
```

(from the repository root; in PowerShell use `${PWD}` instead of `%CD%`).

### Building without Docker

With the Bebbo toolchain installed natively (`m68k-amigaos-gcc` in the
`PATH`, installed to `/opt/amiga` or `/opt/m68k-amigaos`):

```
cd sp/amigaos
make -j8
```

If libnix's `swapstack.o` is somewhere else, pass
`SWAPSTACK=/path/to/libnix/lib/swapstack.o`.

### Checking the build on the Amiga

Copy `objects/mathtest` to the Amiga and run it from a Shell. Every line
must say `ok`. If the math library is broken, the 3D view comes out turned
by 90 degrees and the player walks on walls; `mathtest` shows it at once.

### What the build does, and why

The details are in comments in `sp/amigaos/Makefile` and the `amiga_*.c`
files. In short:

- **Static game modules.** AmigaOS 3.x has no shared objects, so
  `qagame`, `cgame` and `ui` are linked into the executable.
  `objcopy --redefine-syms` gives every module's global symbols a prefix
  (`ld -r` is broken in this toolchain), and marker objects bracket each
  module's data so `amiga_modules.c` can reset it on every load, as a
  fresh DLL would be.
- **Two alignment modes.** Game code uses `-malign-int` (the `.aas` bot
  files are laid out with 4-byte aligned ints); the OS-facing `amiga_*.c`
  files do not, because the NDK structures assume 2-byte alignment.
  `amiga_layout.c` checks at startup that shared structures agree.
- **libnix fixes** (`amiga_libfix.c`). With `-m68040` libnix's float math
  functions return their result in `d0` while callers read `fp0`,
  `vsprintf` does not terminate empty output, and `%f` misrounds. The
  float functions and `rint` are replaced, and `printf` formatting goes through
  `stb_sprintf`. `-fno-builtin-sin/cos` keeps gcc from fusing `sin`+`cos`
  into libnix's broken `cexp`.
- **`-fomit-frame-pointer` is not used**: it broke the 3D view on hardware
  with an older compiler, and with the current one it gains nothing.
- `botlib/l_script.c` is built with `-O1` because gcc 6.5 crashes on it
  with `-O2 -malign-int`.
- `sp/amigaos/amiga_qgl.h` fills the gaps between the renderer and
  MiniGL (no stencil buffer, no clip planes, GL 1.0 texture formats).

## Credits

- id Software – Return to Castle Wolfenstein and its GPL source release
- Mark Olsen and Sigbjørn Skjæret – the MorphOS port this port is based on
- Hyperion Entertainment – MiniGL
- Steffen Häuser and the PiStorm3D contributors – PiStorm3D / MiniGLV3D
- Stefan "Bebbo" Franke – the m68k-amigaos-gcc toolchain
- Sean Barrett and contributors – stb_sprintf
- Claude Code (Anthropic) – wrote the code of this port

## Licence

This is a **modified version** of the Return to Castle Wolfenstein single
player GPL source code. It is not the original program, and it is not made
or endorsed by id Software or ZeniMax Media.

- The game engine and game code are under the **GNU General Public License
  version 3** with id Software's **additional terms** (see
  [`sp/COPYING.txt`](sp/COPYING.txt)). The additional terms require, among
  other things, that copyright and legal notices are kept, that modified
  versions are clearly marked as modified, and they grant no right to use
  id Software's or ZeniMax's names, logos or trademarks.
- The MorphOS port files (`sp/morphos/`) and the AmigaOS files derived from
  them (`sp/amigaos/`) are under the GNU GPL version 2 or later, copyright
  (C) 2005, 2010 Mark Olsen (and 2006 Sigbjørn Skjæret for
  `libnix_so.c`). Their copyright notices are kept in the files, as the
  licence requires. The combined program is distributed under the GPL
  version 3 with the additional terms above.
- `sp/amigaos/thirdparty/stb_sprintf.h` is public domain or MIT.
- MiniGL (the PiStorm3D SDK and `minigl.library`, not included) is under
  the Hyperion MiniGL Open Source License: MiniGL may only be used on
  AmigaOS, and modifications must be made available to the Amiga developer
  community.
- `minigl.library` itself is not included.
- "Return to Castle Wolfenstein" and "Wolfenstein" are trademarks of their
  owners and are used here only to describe what the software is.

If you distribute a binary built from this code, the GPL requires that
you also make the corresponding source code available, for example by
pointing to this repository.

---

## Türkçe

Return to Castle Wolfenstein tek oyunculu modunun 68k AmigaOS 3.x portu.
Görüntü `minigl.library` üzerinden çizilir; PiStorm3D (Raspberry Pi 4 / CM4
tabanlı PiStorm, Emu68) için yazıldı. GPL kaynak kodunun MorphOS portu
temel alındı.

**Bu projenin kodunun tamamı [Claude Code](https://claude.com/claude-code)
ile geliştirildi.** Ben her derlemeyi kendi Amiga'mda denedim ve logları
ilettim; Claude Code kaynağı okudu, sorunları buldu ve düzeltmeleri yazdı.

Normalde oyun portlamıyorum; bu benim için deneysel bir proje. Deneyimli
bir Amiga oyun programcısının işi değil ve geliştirilecek çok şey var. İyi
bir programcının bu kod tabanıyla ve PiStorm3D ile harikalar
yaratabileceğine inanıyorum.

Durum (minigl.library 30.0, ilk bölüm, dururken): 640x480'de 47 FPS,
800x600'de 42, 1024x768'de (varsayılan) 36, 1280x720'de 33, 1920x1080'de 19. Hareket ve çatışmada daha
düşük; `r_vertexlight 1` yaklaşık 10 FPS daha kazandırır.

0.4.1 sürümündeki yenilikler: birkaç dakika oynayınca karakterin donması
(dünya devam ederken klavye ve fare tepki vermiyordu, ses takılıyordu)
düzeltildi: oyunun görev önceliği 20'de kalıp diğer her şeyi aç bırakıyordu.
Açık alanlarda daha hızlı (dinamik ışıklar ve 22 kHz ses karıştırma). Dinamik
ışıklar varsayılan olarak yalnız oyunun zorunlu tuttukları
(`r_dynamiclight 2`, açık alanda ~3 FPS daha); meşale, ateş ve namlu
alevinin çevreyi aydınlatması için konsolda `/r_dynamiclight 1` (kaydedilir).
GL_ADD açık (`r_ext_texture_env_add 0` kapatır).

0.4 sürümündeki yenilikler: donanımda (minigl 29.3 ve 30.0) denendi;
640x480'de yaklaşık 41 → 47 FPS (yeni derleyici ve FPU kodu, Emu68 saati,
minigl 30.0); bölüm yüklemesi yaklaşık 4 saniye kısaldı; 1280x720 ve
1920x1080 menüden seçilebiliyor; varsayılan çözünürlük 1024x768, dokular
varsayılan olarak tam boy (r_picmip 0, fps farkı yok) ve amiga.cfg artık
r_mode, r_vertexlight ve r_picmip ayarlamıyor; kaybolan konsol yazıları, `+map` ile
açılışta logoda kalma ve ölümcül hatadan sonra açık kalan MiniGL ekranı
düzeltildi; Workbench simgesi eklendi (64x64 GlowIcon, 32 renk).

0.2 sürümündeki yenilikler: sesteki çıtırtı giderildi (AHI tamponu 93 ms
yerine yaklaşık 0,75 saniye), ses belleği varsayılan olarak 96 MB (çatışmalar
daha akıcı), 1 GB bellekli sistemlerde pk3 dosyaları için otomatik RAM
önbelleği (sonraki harita yüklemeleri yaklaşık %20 daha hızlı) ve
amiga.cfg'deki "Unknown command lower" uyarısının düzeltmesi.
Menüler, bölümler, ara sahneler, ses, klavye, fare ve konsol çalışıyor.

Kurulum ve derleme adımları yukarıda (İngilizce) ayrıntılı olarak
anlatılmıştır; kısa Türkçe kurulum notu `sp/amigaos/OKUBENI.txt`
dosyasındadır. Oyun verisi (pk3 dosyaları) bu depoda yoktur ve
dağıtılamaz.

Lisans: GPL v3 ve id Software'in ek şartları; MorphOS portundan türeyen
dosyalar GPL v2 veya üstü (Mark Olsen). Ayrıntılar yukarıdaki *Licence*
bölümündedir.

MiniGL / PiStorm3D geliştiricisine iletilmek üzere hazırlanan notlar
[`MINIGL_NOTES.md`](MINIGL_NOTES.md) dosyasındadır.
