Return to Castle Wolfenstein single player for AmigaOS 3.x
============================================================

A 68k port based on the MorphOS port, rendering through minigl.library.
Written for PiStorm3D (Raspberry Pi 4 / CM4 PiStorm, Emu68). Tested on
AmigaOS 3.2 with PiStorm3D, which gives about 40 fps at 640x480 in the
first level (20-38 fps while moving); r_vertexlight 1 adds about 10.

Requirements
------------
- AmigaOS 3.x, a 68040 or better with an FPU (Emu68 qualifies)
- RTG (Picasso96 or CyberGraphX)
- minigl.library 29.1 or newer in LIBS: (from PiStorm3D, not included
  here). Versions 0.1 and 0.2 of this port need the older 27.x library.
- AHI for sound
- Plenty of Fast RAM: about 200 MB free is recommended (25 MB for the
  program, 96 MB for sound, the rest for game data)

Installing
----------
1. Copy the main/ directory of a PC installation patched to 1.41
   (pak0.pk3, sp_pak1.pk3, sp_pak2.pk3, sp_pak3.pk3 at least) into a
   directory, e.g. Games:RTCW/main.
2. Copy rtcw-sp into Games:RTCW, and amiga.cfg (main/amiga.cfg in the
   release archive, sp/amigaos/amiga.cfg in the source)
   into Games:RTCW/main.
3. Start it from a Shell in that directory:
     cd Games:RTCW
     rtcw-sp +exec amiga.cfg
   To start a level directly: rtcw-sp +exec amiga.cfg +devmap escape1

To make the settings permanent, add "exec amiga.cfg" to main/autoexec.cfg
(create it if it does not exist).

Notes
-----
- Console commands need a leading slash while a level is running
  (/god, /cg_drawFPS 1); without it the line is sent as chat. Cheats need
  the level to be started with +devmap.
- Sound: Paula is fine. AHI through a Zorro/clockport sound card costs a
  lot of speed on PiStorm (about 40 -> 24 fps); use a Paula mode, e.g.
  "Paula: HiFi 14 bit stereo++", in AHI prefs.
- MiniGL prints a warning about a small stack at startup. It looks at the
  Shell's setting; the game switches to its own 2 MB stack, so the warning
  can be ignored.
- Loading should be faster from PFS3 or SFS than from FFS (not measured):
  the game seeks a lot inside the large pak0.pk3, which is slow on FFS.
- Loading a level takes about 20-25 s on FFS (measured on escape1). With
  1 GB of RAM the pk3 RAM cache (fs_ramcache) makes later loads about 20%
  faster.

Useful settings (on the command line as +set name value, or in the
console):
  r_vertexlight 1    no lightmaps: about 10 fps faster, flatter lighting
  com_soundMegs 96   sound memory in MB (the default on AmigaOS; less made
                     fights stutter). On machines with little Fast RAM start
                     with +set com_soundMegs 24
  fs_ramcache -1     RAM cache for files read from the pk3s, in MB, so
                     later map loads come from memory. -1 (default): 256 MB
                     if at least 600 MB of Fast RAM are free, else off.
                     0 turns it off, e.g. 400 sets 400 MB.
  r_mode 3           640x480 (the default); r_mode 4 is 800x600,
                     r_mode 13 is 1280x720, r_mode 14 is 1920x1080
                     (a matching RTG screen mode is needed in fullscreen)
  r_fullscreen 0     windowed on the Workbench screen
  r_colorbits 16     16 (default), 24 or 32
  r_depthbits 24     16 or 24 (default)
  sndspeed 22050     AHI mixing rate
  developer 1        loading and shutdown statistics in the console
  r_amigaspeeds 1    per-frame MiniGL timing (slows the game down;
                     r_amigaspeeds 2 adds per-surface timing, much slower)
Lowering the resolution does not help: 320x240 is slower than 640x480.

Not supported
-------------
- Networking (single player only; the loopback connection works)
- Stencil shadows (r_shadows 2) and r_measureOverdraw: no stencil buffer
- Mirrors and portals render without their clip plane
- Gamma control: use r_intensity / r_overBrightBits instead
- The mouse pointer is not shown in windowed mode (Intuition pointer calls
  lock up while a MiniGL context is open)

Building
--------
  ./build.sh         (runs make in the amigadev/m68k-amigaos-gcc Docker image)
  ./build.sh clean

The output is objects/rtcw-sp. See the Makefile for how the three game
modules are linked into the executable. It needs the PiStorm3D 29.1 SDK
(../../PiStorm3D/SDK by default).
