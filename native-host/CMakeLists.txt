cmake_minimum_required(VERSION 3.22)
project(SylarQuestPrivateCore LANGUAGES C)
if(NOT ANDROID OR NOT ANDROID_ABI STREQUAL "arm64-v8a")
 message(FATAL_ERROR "Android arm64-v8a is required")
endif()
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_OBJECT_PATH_MAX 240)
set(CMAKE_NINJA_FORCE_RESPONSE_FILE ON CACHE BOOL "" FORCE)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
foreach(name DKC1_SOURCE SYLAR_PRIVATE_CONFIG SYLAR_PPU_COPY)
 if(NOT EXISTS "${${name}}")
  message(FATAL_ERROR "Missing private input ${name}")
 endif()
endforeach()
if(SYLAR_OBJECT_LIST)
 # Advanced incremental path: reuse the owner's original verified PIC objects.
 include("${SYLAR_OBJECT_LIST}")
 set_source_files_properties(${SYLAR_CORE_OBJECTS} PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
else()
 set(SNESRECOMP_ROOT "${DKC1_SOURCE}/snesrecomp")
 set(SNESRECOMP_ENABLE_TRACE OFF CACHE BOOL "" FORCE)
 set(SNESRECOMP_ENABLE_MODS OFF CACHE BOOL "" FORCE)
 set(SNESRECOMP_ENABLE_NET OFF CACHE BOOL "" FORCE)
 set(SNES_COSIM OFF CACHE BOOL "" FORCE)
 set(SNESRECOMP_ENABLE_FRAME_FINGERPRINTS OFF CACHE BOOL "" FORCE)
 set(SNESRECOMP_ENABLE_PPU_DMA_HISTORY OFF CACHE BOOL "" FORCE)
 set(SNESRECOMP_ENABLE_DISPATCH_HISTORY OFF CACHE BOOL "" FORCE)
 include("${SNESRECOMP_ROOT}/runner/runner.cmake")
 list(REMOVE_ITEM SNESRECOMP_RUNNER_SOURCES
  "${SNESRECOMP_ROOT}/runner/src/launcher.c"
  "${SNESRECOMP_ROOT}/runner/src/launcher_cache.c"
  "${SNESRECOMP_ROOT}/runner/src/launcher_picker.c"
  "${SNESRECOMP_ROOT}/runner/src/keybinds.c"
  "${SNESRECOMP_ROOT}/runner/src/host_report.c"
  "${SNESRECOMP_ROOT}/runner/src/snes/ppu.c")
 set(names dkc1_game.c dkc1_video.c dkc1_baby_kong.c
  dkc1_baby_kong_animation.c dkc1_baby_kong_movement.c dkc1_dixie_mod.c
  dkc1_margin_proxy.c dkc1_ws_trace.c input_playback.c wram_dump.c
  dkc1_script.c dkc1_debug_dump.c dkc1_flight_recorder.c dkc1_blank_scan.c
  dkc1_invariant_monitor.c verified_rom.c)
 foreach(name IN LISTS names)
  list(APPEND SYLAR_CORE_OBJECTS "${DKC1_SOURCE}/runner/${name}")
 endforeach()
 file(GLOB generated CONFIGURE_DEPENDS "${SYLAR_PRIVATE_GENERATED}/*.c")
 if(NOT generated)
  message(FATAL_ERROR "Generate private C from your verified ROM first")
 endif()
 list(APPEND SYLAR_CORE_OBJECTS ${SNESRECOMP_RUNNER_SOURCES} ${generated})
endif()
add_library(sylar_dkc1 SHARED game_bridge.c host_util.c private_sram.c layer_capture.c
 "${SYLAR_PPU_COPY}" ${SYLAR_CORE_OBJECTS})
target_include_directories(sylar_dkc1 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${DKC1_SOURCE}/runner"
 "${DKC1_SOURCE}/snesrecomp/runner/src" "${DKC1_SOURCE}/snesrecomp/runner/src/snes"
 "${SYLAR_PRIVATE_CONFIG}")
target_compile_definitions(sylar_dkc1 PRIVATE _POSIX_C_SOURCE=200809L SNESRECOMP_TRACE=0
 SNESRECOMP_REVERSE_DEBUG=0 SNESRECOMP_ENABLE_MODS=0
 SNESRECOMP_EXTERNAL_RAM_ROUTINE_GUARDS=1 SYSTEM_VOLUME_MIXER_AVAILABLE=0
 DKC1_BUILD_COMMIT="3eb9a10c49cb210df2bd63addafe2a78cf9f4e22"
 DKC1_BUILD_CONFIG="android-arm64-source-port")
target_compile_options(sylar_dkc1 PRIVATE -Wall -Wextra -Wno-unused-function)
set_source_files_properties(game_bridge.c host_util.c private_sram.c layer_capture.c PROPERTIES COMPILE_OPTIONS "-Werror")
target_link_options(sylar_dkc1 PRIVATE -Wl,--no-undefined -Wl,--wrap=Dkc1DrawPpuFrame
 -Wl,--wrap=Dkc1BabyKongPrepareFrame -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/exports.map)
target_link_libraries(sylar_dkc1 PRIVATE m log aaudio)
