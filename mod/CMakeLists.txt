# UBWCoop injector: dwmapi.dll proxy + MinHook.
# Configure: cmake -G Ninja -S mod -B build -DCMAKE_TOOLCHAIN_FILE=$PWD/toolchain/clang-cl-xwin.cmake -DCMAKE_BUILD_TYPE=Release  (toolchain path must be absolute)
cmake_minimum_required(VERSION 3.20)
project(ubwcoop LANGUAGES C CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  message(FATAL_ERROR "Build with toolchain/clang-cl-xwin.cmake (x64 clang-cl); naked asm stubs need clang")
endif()

# Static release CRT for every config: nothing extra to install into the Proton prefix,
# and xwin ships no debug CRT.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- MinHook (vendored, tag v1.3.4). Third-party: default warnings, not -WX. ---
set(MINHOOK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/minhook")
add_library(minhook STATIC
  "${MINHOOK_DIR}/src/buffer.c"
  "${MINHOOK_DIR}/src/hook.c"
  "${MINHOOK_DIR}/src/trampoline.c"
  "${MINHOOK_DIR}/src/hde/hde64.c")
target_include_directories(minhook PUBLIC "${MINHOOK_DIR}/include")

# --- Proxy DLL: must be named dwmapi.dll ---
add_library(ubwcoop SHARED
  src/ue/config.cpp
  src/ubw/flags.cpp
  src/ubw/guards.cpp
  src/ubw/crash_trace.cpp
  src/ubw/stall_c303.cpp
  src/ubw/frame_c330.cpp
  src/ubw/radio_c322.cpp
  src/ubw/bark_c451.cpp
  src/ubw/target_c532.cpp
  src/ubw/floor_c535.cpp
  src/ubw/query_bp_c545.cpp
  src/ubw/diag_crowd_c575.cpp
  src/ubw/query_c534.cpp
  src/ubw/weapon_c494.cpp
  src/ubw/cave_c493.cpp
  src/ubw/insta_c361.cpp
  src/ubw/rack_c556.cpp
  src/ubw/player0_c321.cpp
  src/ubw/fallzone_c338.cpp
  src/ubw/render_c337.cpp
  src/ubw/input_diag.cpp
  src/ubw/pawn_collision.cpp
  src/ubw/dropship.cpp
  src/ubw/sprint.cpp
  src/ubw/sync_registry.cpp
  src/ubw/attributes.cpp
  src/ubw/weapons.cpp
  src/ubw/script_c310.cpp
  src/ubw/restore_c143.cpp
  src/ubw/ammo.cpp
  src/ubw/fire.cpp
  src/ubw/chan_ops.cpp
  src/ubw/audit_npc.cpp
  src/ubw/enemies.cpp
  src/ubw/net_budget.cpp
  src/ubw/remote_body.cpp
  src/ubw/vehicles.cpp
  src/ubw/turret_sync.cpp
  src/ubw/net_drop.cpp
  src/ubw/death.cpp
  src/ubw/restart.cpp
  src/ubw/menu_c350.cpp
  src/ubw/lobby_ui.cpp
  src/ubw/objectives.cpp
  src/ubw/interact.cpp
  src/ubw/testrun.cpp
  src/ubw/autoplay.cpp
  src/ubw/objective_c284.cpp
  src/ubw/grenades.cpp
  src/ubw/proj_c132.cpp
  src/ubw/fx_c136.cpp
  src/ubw/fx_c136_net.cpp
  src/ubw/fx_watch.cpp
  src/ubw/cine_skip.cpp
  src/ubw/intro_c135.cpp
  src/ubw/jump_c94.cpp
  src/ubw/door_c95.cpp
  src/ubw/door_c161.cpp
  src/ubw/horde_c606.cpp
  src/ubw/audio_c604.cpp
  src/ubw/satchel_c408.cpp
  src/ubw/grenade_c158.cpp
  src/ubw/spawn_c169.cpp
  src/ubw/pause_c96.cpp
  src/ubw/world_c98.cpp
  src/ubw/world_c132.cpp
  src/ubw/digest_c132.cpp
  src/ubw/hit_c132.cpp
  src/ubw/claim_c157.cpp
  src/ubw/marker_c166.cpp
  src/ubw/round_c181.cpp
  src/ubw/decal_c132.cpp
  src/ubw/ext_c132.cpp
  src/ubw/gib_c137.cpp
  src/ubw/change_c137.cpp
  src/ubw/pose_c132.cpp
  src/ubw/ledger_c132.cpp
  src/ubw/hold.cpp
  src/ubw/save_c121.cpp
  src/ubw/player_c99.cpp
  src/ubw/score_c475.cpp
  src/ubw/waves_c503.cpp
  src/ubw/begin_c138.cpp
  src/ubw/anim_c104.cpp
  src/ubw/player_marker.cpp
  src/ubw/radar_c555.cpp
  src/ubw/seat_c588.cpp
  src/ubw/bug_scale.cpp
  src/ubw/attack_pace.cpp
  src/ubw/difficulty.cpp
  src/ubw/enemy_c103.cpp
  src/ubw/playlog.cpp
  src/dllmain.cpp
  src/ue/log.cpp
  src/ue/reflect.cpp
  src/ue/actor.cpp
  src/ue/gas.cpp
  src/ue/input.cpp
  src/ue/widget.cpp
  src/ue/hooktable.cpp
  src/ue/install.cpp
  src/ubw/perf_link.cpp
  src/ue/net.cpp
  src/ue/chan.cpp
  src/ue/chan_pack.cpp
  src/proxy.cpp
  src/ubw/hooks_ubw.cpp
  src/autoinput.cpp
  src/cmdfile.cpp
  src/movieskip.cpp
  src/winmsg.cpp
  src/galaxycfg.cpp
  src/lobby.cpp
  src/upnp.cpp
  src/dwmapi.def)
target_include_directories(ubwcoop PRIVATE src)
set_target_properties(ubwcoop PROPERTIES OUTPUT_NAME dwmapi PREFIX "")
target_compile_definitions(ubwcoop PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_compile_options(ubwcoop PRIVATE /W4 /WX /EHs-c- /GR-)
target_link_options(ubwcoop PRIVATE /WX /MAP) # map file (dwmapi.map) names mod functions in perf profiles; the DLL bytes do not change
target_link_libraries(ubwcoop PRIVATE minhook kernel32)

# --- Card 578: performance mod, ubwperf.dll. Loaded by dwmapi.dll in co-op, or by the game as winhttp.dll alone. ---
add_library(ubwperf SHARED
  src/perf/perf_main.cpp
  src/perf/perf_sig_c568.cpp
  src/perf/perf_save_c531.cpp
  src/perf/perf_component_c536.cpp
  src/perf/perf_target_c538.cpp
  src/perf/perf_move_c539.cpp
  src/perf/perf_tree_c544.cpp
  src/perf/perf_overlap_c548.cpp
  src/perf/perf_kinematic_c573.cpp
  src/perf/perf_niagara_c573.cpp
  src/perf/perf_spin_c576.cpp
  src/perf/perf_navgen_c577.cpp
  src/perf/perf_navdata_c593.cpp
  src/perf/perf_target_c594.cpp
  src/perf/proxy_winhttp.cpp
  src/ue/config.cpp
  src/ue/log.cpp
  src/ue/reflect.cpp
  src/ue/hooktable.cpp
  src/perf/winhttp.def)
target_include_directories(ubwperf PRIVATE src)
set_target_properties(ubwperf PROPERTIES OUTPUT_NAME ubwperf PREFIX "")
target_compile_definitions(ubwperf PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_compile_options(ubwperf PRIVATE /W4 /WX /EHs-c- /GR-)
target_link_options(ubwperf PRIVATE /WX /MAP)
target_link_libraries(ubwperf PRIVATE minhook kernel32)
