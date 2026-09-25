
CC=ppc-morphos-gcc
CXX=ppc-morphos-g++
STRIP=ppc-morphos-strip

CFLAGS=-Wall -O2 -noixemul -D__MORPHOS_SHAREDLIBS -fsigned-char -fno-strict-aliasing -DNDEBUG -DBOTLIB $(BUILDCFLAGS)
CXXFLAGS=$(CFLAGS) -fno-exceptions
STRIPFLAGS=--strip-unneeded --remove-section=.comment

SOKEEPSYMBOLS=-K dllEntry -K vmMain -K rtcwmp_so_init -K rtcwmp_so_deinit -K rtcw_so_get_segment_info

all: build-rtcw-mp build-cgame build-qagame build-ui

.dirscreated:
	mkdir -p botai botlib cgame client game jpeg-6 morphos qcommon renderer server splines ui unix
	touch .dirscreated

RTCWMPOBJS = \
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
	morphos/morphos_glimp.o \
	morphos/morphos_in.o \
	morphos/morphos_main.o \
	morphos/morphos_snd.o \
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
	unix/unix_net.o \
	unix/unix_shared.o

build-rtcw-mp:
	mkdir -p objects/rtcw-mp
	cd objects/rtcw-mp && $(MAKE) -f ../../Makefile rtcw-mp VPATH=../../../

rtcw-mp: .dirscreated $(RTCWMPOBJS)
	$(CC) $(CFLAGS) $(RTCWMPOBJS) -o $@.db
	$(STRIP) $(STRIPFLAGS) $@.db -o $@

###

CGAMEOBJS = \
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
	cgame/cg_spawn.o \
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
	morphos/libnix_so.o \
	ui/ui_shared.o

build-cgame:
	mkdir -p objects/cgame
	cd objects/cgame && $(MAKE) -f ../../Makefile cgame_mp_morphos_ppc.so VPATH=../../../ BUILDCFLAGS="-DCGAMEDLL $(SHLIBCFLAGS)"

cgame_mp_morphos_ppc.so: .dirscreated $(CGAMEOBJS)
	$(CC) -nostartfiles $(CFLAGS) $(CGAMEOBJS) -o $@.db
	$(STRIP) $(STRIPFLAGS) $(SOKEEPSYMBOLS) $@.db -o $@

###

QAGAMEOBJS = \
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
	game/g_antilag.o \
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
	game/g_vehicles.o \
	game/g_weapon.o \
	game/q_math.o \
	game/q_shared.o \
	morphos/libnix_so.o \
	qcommon/ugly_so_segment_hack.o

build-qagame:
	mkdir -p objects/qagame
	cd objects/qagame && $(MAKE) -f ../../Makefile qagame_mp_morphos_ppc.so VPATH=../../../ BUILDCFLAGS="-DGAMEDLL $(SHLIBCFLAGS)"

qagame_mp_morphos_ppc.so: .dirscreated $(QAGAMEOBJS)
	$(CC) -nostartfiles $(CFLAGS) $(QAGAMEOBJS) ../../linkerscript -o $@.db
	$(STRIP) $(STRIPFLAGS) $(SOKEEPSYMBOLS) $@.db -o $@

###

UIOBJS = \
	game/bg_misc.o \
	game/q_math.o \
	game/q_shared.o \
	morphos/libnix_so.o \
	ui/ui_atoms.o \
	ui/ui_gameinfo.o \
	ui/ui_main.o \
	ui/ui_players.o \
	ui/ui_shared.o \
	ui/ui_syscalls.o \
	ui/ui_util.o

build-ui:
	mkdir -p objects/ui
	cd objects/ui && $(MAKE) -f ../../Makefile ui_mp_morphos_ppc.so VPATH=../../../ BUILDCFLAGS="$(SHLIBCFLAGS)"

ui_mp_morphos_ppc.so: .dirscreated $(UIOBJS)
	$(CC) -nostartfiles $(CFLAGS) $(UIOBJS) -o $@.db
	$(STRIP) $(STRIPFLAGS) $(SOKEEPSYMBOLS) $@.db -o $@

###

clean:
	rm -rf objects

.PHONY: clean build-et
