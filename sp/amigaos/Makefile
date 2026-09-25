# Return to Castle Wolfenstein single player for AmigaOS 3.x (68k),
# rendering through minigl.library (PiStorm3D or any other implementation).
#
# Needs bebbo's m68k-amigaos-gcc toolchain; build.sh runs this Makefile in
# the amigadev/m68k-amigaos-gcc Docker image. Output: objects/rtcw-sp.
#
# The game modules (qagame, cgame, ui) cannot be shared objects on
# AmigaOS 3.x, so they are linked into the executable. Every global symbol
# a module defines gets the module's name as a prefix (objcopy
# --redefine-syms), so their private copies of q_shared.c, bg_*.c and
# ui_shared.c do not clash with each other or with the engine. See
# amiga_modules.c for how a module is reset between loads.

CC      = m68k-amigaos-gcc
CXX     = m68k-amigaos-g++
NM      = m68k-amigaos-nm
OBJCOPY = m68k-amigaos-objcopy
STRIP   = m68k-amigaos-strip

SRC     = ..
MINIGL ?= ../../PiStorm3D/dev
SWAPSTACK ?= /opt/amiga/m68k-amigaos/libnix/lib/swapstack.o

CPU     = -m68040

# -fno-math-errno lets gcc use the FPU's fsqrt inline instead of calling sqrt.
# It also lets gcc merge sin(x) and cos(x) of the same x into one call to
# cexp(), and libnix's cexp gives wrong results (tests/mathtest.c,
# AngleVectors). -fno-builtin-cexp does not stop that, so sin and cos are
# not treated as builtins at all; the 68040 has no fsin/fcos to inline.
# No -fomit-frame-pointer: it broke the view again on hardware (2026-09-25),
# likely the same float-return mismatch amiga_libfix.c works around.
BASEFLAGS = -O2 -noixemul $(CPU) -fsigned-char -fno-strict-aliasing -fno-math-errno \
            -fno-builtin-sin -fno-builtin-cos -fno-builtin-sinf -fno-builtin-cosf \
            -DNDEBUG -DBOTLIB -Iminigl-shim -I$(MINIGL)/include

# Game code: 4-byte aligned ints, as on the PC, so that data files read
# straight into structures (the bots' .aas files, for one) line up.
GAME_CFLAGS = $(BASEFLAGS) -malign-int

# Code that talks to the operating system: the NDK structures assume the
# 68k's native 2-byte alignment, so no -malign-int here.
OS_CFLAGS = $(BASEFLAGS)

# Modules additionally need -fno-common, so every variable they define
# lands in .data or .bss between the module's marker objects.
MOD_CFLAGS = $(GAME_CFLAGS) -fno-common

CXXFLAGS_EXTRA = -fno-exceptions -fno-rtti

LIBS = -L$(MINIGL)/lib -lminigl -lm

OBJDIR = objects

###########################################################################
# Engine

ENGINE_OBJS = \
	botlib/be_aas_bspq3.o \
	botlib/be_aas_cluster.o \
	botlib/be_aas_debug.o \
	botlib/be_aas_entity.o \
	botlib/be_aas_file.o \
	botlib/be_aas_main.o \
	botlib/be_aas_move.o \
	botlib/be_aas_optimize.o \
	botlib/be_aas_reach.o \
	botlib/be_aas_route.o \
	botlib/be_aas_routealt.o \
	botlib/be_aas_routetable.o \
	botlib/be_aas_sample.o \
	botlib/be_ai_char.o \
	botlib/be_ai_chat.o \
	botlib/be_ai_gen.o \
	botlib/be_ai_goal.o \
	botlib/be_ai_move.o \
	botlib/be_ai_weap.o \
	botlib/be_ai_weight.o \
	botlib/be_ea.o \
	botlib/be_interface.o \
	botlib/l_crc.o \
	botlib/l_libvar.o \
	botlib/l_log.o \
	botlib/l_memory.o \
	botlib/l_precomp.o \
	botlib/l_script.o \
	botlib/l_struct.o \
	client/cl_cgame.o \
	client/cl_cin.o \
	client/cl_console.o \
	client/cl_input.o \
	client/cl_keys.o \
	client/cl_main.o \
	client/cl_net_chan.o \
	client/cl_parse.o \
	client/cl_scrn.o \
	client/cl_ui.o \
	client/snd_adpcm.o \
	client/snd_dma.o \
	client/snd_mem.o \
	client/snd_mix.o \
	client/snd_wavelet.o \
	game/q_math.o \
	game/q_shared.o \
	jpeg-6/jcapimin.o \
	jpeg-6/jccoefct.o \
	jpeg-6/jccolor.o \
	jpeg-6/jcdctmgr.o \
	jpeg-6/jchuff.o \
	jpeg-6/jcinit.o \
	jpeg-6/jcmainct.o \
	jpeg-6/jcmarker.o \
	jpeg-6/jcmaster.o \
	jpeg-6/jcomapi.o \
	jpeg-6/jcparam.o \
	jpeg-6/jcphuff.o \
	jpeg-6/jcprepct.o \
	jpeg-6/jcsample.o \
	jpeg-6/jdapimin.o \
	jpeg-6/jdapistd.o \
	jpeg-6/jdatasrc.o \
	jpeg-6/jdcoefct.o \
	jpeg-6/jdcolor.o \
	jpeg-6/jddctmgr.o \
	jpeg-6/jdhuff.o \
	jpeg-6/jdinput.o \
	jpeg-6/jdmainct.o \
	jpeg-6/jdmarker.o \
	jpeg-6/jdmaster.o \
	jpeg-6/jdpostct.o \
	jpeg-6/jdsample.o \
	jpeg-6/jdtrans.o \
	jpeg-6/jerror.o \
	jpeg-6/jfdctflt.o \
	jpeg-6/jidctflt.o \
	jpeg-6/jmemmgr.o \
	jpeg-6/jmemnobs.o \
	jpeg-6/jutils.o \
	qcommon/cm_load.o \
	qcommon/cm_patch.o \
	qcommon/cm_polylib.o \
	qcommon/cm_test.o \
	qcommon/cm_trace.o \
	qcommon/cmd.o \
	qcommon/common.o \
	qcommon/cvar.o \
	qcommon/files.o \
	qcommon/huffman.o \
	qcommon/md4.o \
	qcommon/msg.o \
	qcommon/net_chan.o \
	qcommon/unzip.o \
	qcommon/vm.o \
	qcommon/vm_interpreted.o \
	renderer/tr_animation.o \
	renderer/tr_backend.o \
	renderer/tr_bsp.o \
	renderer/tr_cmds.o \
	renderer/tr_cmesh.o \
	renderer/tr_curve.o \
	renderer/tr_flares.o \
	renderer/tr_font.o \
	renderer/tr_image.o \
	renderer/tr_init.o \
	renderer/tr_light.o \
	renderer/tr_main.o \
	renderer/tr_marks.o \
	renderer/tr_mesh.o \
	renderer/tr_model.o \
	renderer/tr_noise.o \
	renderer/tr_scene.o \
	renderer/tr_shade.o \
	renderer/tr_shade_calc.o \
	renderer/tr_shader.o \
	renderer/tr_shadows.o \
	renderer/tr_sky.o \
	renderer/tr_surface.o \
	renderer/tr_world.o \
	server/sv_bot.o \
	server/sv_ccmds.o \
	server/sv_client.o \
	server/sv_game.o \
	server/sv_init.o \
	server/sv_main.o \
	server/sv_net_chan.o \
	server/sv_snapshot.o \
	server/sv_world.o \
	splines/q_parse.o \
	splines/splines.o \
	splines/util_str.o \
	unix/unix_shared.o \
	amigaos/amiga_layout.o \
	amigaos/amiga_modules.o \
	amigaos/amiga_net.o

OS_OBJS = \
	amigaos/amiga_glimp.o \
	amigaos/amiga_in.o \
	amigaos/amiga_layout.o \
	amigaos/amiga_libfix.o \
	amigaos/amiga_main.o \
	amigaos/amiga_snd.o

###########################################################################
# Game modules. The marker objects must stay first and last.

qagame_OBJS = \
	amigaos/amiga_modmark_begin.o \
	botai/ai_chat.o \
	botai/ai_cmd.o \
	botai/ai_dmnet.o \
	botai/ai_dmq3.o \
	botai/ai_main.o \
	botai/ai_team.o \
	game/ai_cast.o \
	game/ai_cast_characters.o \
	game/ai_cast_debug.o \
	game/ai_cast_events.o \
	game/ai_cast_fight.o \
	game/ai_cast_func_attack.o \
	game/ai_cast_func_boss1.o \
	game/ai_cast_funcs.o \
	game/ai_cast_script.o \
	game/ai_cast_script_actions.o \
	game/ai_cast_script_ents.o \
	game/ai_cast_sight.o \
	game/ai_cast_think.o \
	game/bg_animation.o \
	game/bg_misc.o \
	game/bg_pmove.o \
	game/bg_slidemove.o \
	game/g_active.o \
	game/g_alarm.o \
	game/g_bot.o \
	game/g_client.o \
	game/g_cmds.o \
	game/g_combat.o \
	game/g_items.o \
	game/g_main.o \
	game/g_mem.o \
	game/g_misc.o \
	game/g_missile.o \
	game/g_mover.o \
	game/g_props.o \
	game/g_save.o \
	game/g_script.o \
	game/g_script_actions.o \
	game/g_session.o \
	game/g_spawn.o \
	game/g_svcmds.o \
	game/g_syscalls.o \
	game/g_target.o \
	game/g_team.o \
	game/g_tramcar.o \
	game/g_trigger.o \
	game/g_utils.o \
	game/g_weapon.o \
	game/q_math.o \
	game/q_shared.o \
	amigaos/amiga_modmark_end.o

cgame_OBJS = \
	amigaos/amiga_modmark_begin.o \
	cgame/cg_consolecmds.o \
	cgame/cg_draw.o \
	cgame/cg_drawtools.o \
	cgame/cg_effects.o \
	cgame/cg_ents.o \
	cgame/cg_event.o \
	cgame/cg_flamethrower.o \
	cgame/cg_info.o \
	cgame/cg_localents.o \
	cgame/cg_main.o \
	cgame/cg_marks.o \
	cgame/cg_newDraw.o \
	cgame/cg_particles.o \
	cgame/cg_players.o \
	cgame/cg_playerstate.o \
	cgame/cg_predict.o \
	cgame/cg_scoreboard.o \
	cgame/cg_servercmds.o \
	cgame/cg_snapshot.o \
	cgame/cg_sound.o \
	cgame/cg_syscalls.o \
	cgame/cg_trails.o \
	cgame/cg_view.o \
	cgame/cg_weapons.o \
	game/bg_animation.o \
	game/bg_misc.o \
	game/bg_pmove.o \
	game/bg_slidemove.o \
	game/q_math.o \
	game/q_shared.o \
	ui/ui_shared.o \
	amigaos/amiga_modmark_end.o

ui_OBJS = \
	amigaos/amiga_modmark_begin.o \
	game/bg_misc.o \
	game/q_math.o \
	game/q_shared.o \
	ui/ui_atoms.o \
	ui/ui_gameinfo.o \
	ui/ui_main.o \
	ui/ui_players.o \
	ui/ui_shared.o \
	ui/ui_syscalls.o \
	ui/ui_util.o \
	amigaos/amiga_modmark_end.o

qagame_DEFS = -DGAMEDLL
cgame_DEFS  = -DCGAMEDLL
ui_DEFS     =

###########################################################################

all: $(OBJDIR)/rtcw-sp

ENGINE = $(addprefix $(OBJDIR)/engine/,$(ENGINE_OBJS))
OS     = $(addprefix $(OBJDIR)/os/,$(OS_OBJS))

$(OBJDIR)/engine/amigaos/amiga_layout.o: EXTRA_DEFS = -DAMIGA_LAYOUT_ENGINE

# gcc 6.5.0b crashes (internal compiler error) on this file's long double
# to unsigned long conversion with -O2 and -malign-int together.
$(OBJDIR)/engine/botlib/l_script.o: EXTRA_DEFS = -O1

# Replaces C library functions; see the comments in the file.
# Built without -malign-int: it shares no structures with the game, and
# gcc 6.5.0b crashes on stb_sprintf.h with -O2 and -malign-int together.
$(OBJDIR)/os/amigaos/amiga_libfix.o: EXTRA_DEFS = -fno-builtin

$(OBJDIR)/engine/%.o: $(SRC)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(GAME_CFLAGS) $(EXTRA_DEFS) -c $< -o $@

$(OBJDIR)/engine/%.o: $(SRC)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(GAME_CFLAGS) $(CXXFLAGS_EXTRA) -c $< -o $@

$(OBJDIR)/os/%.o: $(SRC)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(OS_CFLAGS) $(EXTRA_DEFS) -c $< -o $@

# $(1) = module name
define MODULE_RULES
$(1)_RAW = $$(addprefix $$(OBJDIR)/$(1)/raw/,$$($(1)_OBJS))
$(1)_REN = $$(addprefix $$(OBJDIR)/$(1)/ren/,$$($(1)_OBJS))

$$(OBJDIR)/$(1)/raw/%.o: $$(SRC)/%.c
	@mkdir -p $$(dir $$@)
	$$(CC) $$(MOD_CFLAGS) $$($(1)_DEFS) -c $$< -o $$@

# Every global the module defines, mapped to <module>_<name>. Undefined
# references (the C library) are left alone.
$$(OBJDIR)/$(1)/syms.txt: $$($(1)_RAW)
	$$(NM) -g --defined-only $$^ | awk '$$$$2 ~ /^[TDBR]$$$$/ { print $$$$3 " _$(1)_" substr($$$$3, 2) }' | sort -u > $$@

$$(OBJDIR)/$(1)/ren/%.o: $$(OBJDIR)/$(1)/raw/%.o $$(OBJDIR)/$(1)/syms.txt
	@mkdir -p $$(dir $$@)
	$$(OBJCOPY) --redefine-syms=$$(OBJDIR)/$(1)/syms.txt $$< $$@
endef

$(foreach m,qagame cgame ui,$(eval $(call MODULE_RULES,$(m))))

MODULES = $(qagame_REN) $(cgame_REN) $(ui_REN)

$(OBJDIR)/rtcw-sp: $(ENGINE) $(OS) $(MODULES)
	$(CXX) $(CPU) -noixemul -o $@.db $(ENGINE) $(OS) $(MODULES) $(SWAPSTACK) $(LIBS)
	$(STRIP) --strip-unneeded --remove-section=.comment $@.db -o $@

# Standalone check of the math functions, with the game's flags.
mathtest: $(OBJDIR)/mathtest

$(OBJDIR)/mathtest: tests/mathtest.c $(OBJDIR)/os/amigaos/amiga_libfix.o
	@mkdir -p $(dir $@)
	$(CC) $(GAME_CFLAGS) -o $@ $^ -lm

clean:
	rm -rf $(OBJDIR)

.PHONY: all clean mathtest
.SECONDARY:
