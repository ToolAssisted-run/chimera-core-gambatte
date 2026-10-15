# chimera-core-gambatte: libgambatte, from Gambatte-Speedrun (gambatte-core),
# in miniBox's sandbox, as a Chimera core.
#
# Native reference build (run-native + run-wbx under the build directory):
#     meson setup build/meson-native -Dminibox_dir=<miniBox> && ninja -C build/meson-native
# Guest (core.wbx) build via the cross file waterbox/setup-guest.sh writes
# (needs miniBox's C++ guest toolchain, build/meson-cpp):
#     sh waterbox/setup-guest.sh -m <miniBox> && ninja -C build/meson-guest core.wbx
#
# The files are upstream's libgambatte/SConstruct's, without zip support
# (NO_ZIP: Chimera hands the core the game's own file) and without its C
# interface for other programs (src/cinterface.cpp): waterbox/cinterface.cpp
# is the core's. Both flavours compile the SAME sources; only emulibc.h
# differs (waterbox/native-shim for the reference).
project('chimera-core-gambatte', ['c', 'cpp'],
  default_options: ['c_std=gnu11', 'cpp_std=gnu++11', 'optimization=2', 'debug=false', 'warning_level=0'])

gb = 'extern/gambatte-core/libgambatte'

# the submodule pin is upstream as it is; the patches are laid over it
# (waterbox/apply-patches.sh does nothing to a tree that has them)
run_command(files('waterbox/apply-patches.sh'), check: true)

mb = get_option('minibox_dir')
if mb == ''
  mb_candidate = meson.current_source_dir() / '../chimera/extern/chimera-common-minibox'
  if import('fs').is_dir(mb_candidate)
    mb = mb_candidate
  else
    error('pass -Dminibox_dir=<miniBox checkout> (or keep one at ../chimera/extern/chimera-common-minibox)')
  endif
endif

# upstream's own define (its SConstruct)
defs = ['-DHAVE_STDINT_H', '-DNDEBUG']
# one answer from a floating-point expression on every machine (no fused
# multiply-adds; the Super Game Boy's sound chip filters in floating point)
flags = ['-fwrapv', '-fno-strict-aliasing', '-ffp-contract=off']
add_project_arguments(defs + flags, language: ['c', 'cpp'])

inc = include_directories('waterbox', gb / 'include', gb / 'src', 'extern/gambatte-core/common')

core_sources = files(
  'waterbox/cinterface.cpp',
  gb / 'src/bitmap_font.cpp',
  gb / 'src/cpu.cpp',
  gb / 'src/gambatte.cpp',
  gb / 'src/initstate.cpp',
  gb / 'src/interrupter.cpp',
  gb / 'src/interruptrequester.cpp',
  gb / 'src/loadres.cpp',
  gb / 'src/memory.cpp',
  gb / 'src/newstate.cpp',
  gb / 'src/sound.cpp',
  gb / 'src/state_osd_elements.cpp',
  gb / 'src/statesaver.cpp',
  gb / 'src/tima.cpp',
  gb / 'src/video.cpp',
  gb / 'src/mem/camera.cpp',
  gb / 'src/mem/cartridge.cpp',
  gb / 'src/mem/huc3_chip.cpp',
  gb / 'src/mem/infrared.cpp',
  gb / 'src/mem/memptrs.cpp',
  gb / 'src/mem/pakinfo.cpp',
  gb / 'src/mem/remote.cpp',
  gb / 'src/mem/rtc.cpp',
  gb / 'src/mem/sgb.cpp',
  gb / 'src/mem/time.cpp',
  gb / 'src/mem/mbc/huc1.cpp',
  gb / 'src/mem/mbc/huc3.cpp',
  gb / 'src/mem/mbc/m161.cpp',
  gb / 'src/mem/mbc/mbc0.cpp',
  gb / 'src/mem/mbc/mbc1.cpp',
  gb / 'src/mem/mbc/mbc2.cpp',
  gb / 'src/mem/mbc/mbc3.cpp',
  gb / 'src/mem/mbc/mbc5.cpp',
  gb / 'src/mem/mbc/mmm01.cpp',
  gb / 'src/mem/mbc/pocket_camera.cpp',
  gb / 'src/mem/mbc/wisdom_tree.cpp',
  gb / 'src/mem/snes_spc/dsp.cpp',
  gb / 'src/mem/snes_spc/SNES_SPC.cpp',
  gb / 'src/mem/snes_spc/SNES_SPC_misc.cpp',
  gb / 'src/mem/snes_spc/SNES_SPC_state.cpp',
  gb / 'src/mem/snes_spc/spc.cpp',
  gb / 'src/mem/snes_spc/SPC_DSP.cpp',
  gb / 'src/mem/snes_spc/SPC_Filter.cpp',
  gb / 'src/sound/channel1.cpp',
  gb / 'src/sound/channel2.cpp',
  gb / 'src/sound/channel3.cpp',
  gb / 'src/sound/channel4.cpp',
  gb / 'src/sound/duty_unit.cpp',
  gb / 'src/sound/envelope_unit.cpp',
  gb / 'src/sound/length_counter.cpp',
  gb / 'src/video/ly_counter.cpp',
  gb / 'src/video/lyc_irq.cpp',
  gb / 'src/video/next_m0_time.cpp',
  gb / 'src/video/ppu.cpp',
  gb / 'src/video/sprite_mapper.cpp',
  gb / 'src/file/file.cpp',
  gb / 'src/file/crc32.cpp',
)

if meson.is_cross_build()
  # ---- the guest: core.wbx --------------------------------------------------
  # miniBox's musl and libstdc++ arrive through the cross file. The -u lines
  # name the pthread functions libgcc's unwinder refers to weakly: a weak
  # reference is not enough to pull them out of a static libc, and it would
  # call address zero.
  mbdir = meson.get_external_property('minibox_dir')
  mbuild = meson.get_external_property('minibox_build')
  sr = meson.get_external_property('guest_sysroot')

  executable('core', core_sources,
    name_suffix: 'wbx',
    include_directories: inc,
    link_args: [
      '-static', '-no-pie',
      '-Wl,--eh-frame-hdr', '-Wl,-O2', '-Wl,--no-relax', '-Wl,-z,stack-size=8388608',
      '-T', mbdir / 'source/guest/linkscript.T',
      '-Wl,-u,pthread_once', '-Wl,-u,pthread_cond_wait',
      '-Wl,-u,pthread_cond_broadcast', '-Wl,-u,pthread_key_create',
      '-Wl,-u,pthread_mutexattr_init', '-Wl,-u,pthread_mutexattr_settype',
      '-Wl,-u,pthread_mutexattr_destroy',
      mbuild / 'source/guest/cxxglue.c.o',
      mbuild / 'source/guest/emulibc.c.o',
      '-L' + sr / 'lib', '-lstdc++', '-lgcc', '-lgcc_eh', '-lc'])
else
  # ---- the native reference -------------------------------------------------
  m = meson.get_compiler('c').find_library('m', required: false)
  executable('run-native', core_sources + files('waterbox/run-native.c'),
    include_directories: [include_directories('waterbox/native-shim'), inc],
    c_args: ['-I' + mb / 'source/guest/include', '-I' + mb / 'extern/jsmn'],
    cpp_args: ['-I' + mb / 'source/guest/include', '-I' + mb / 'extern/jsmn'],
    dependencies: [m])

  mbhost = mb / 'build/meson-linux/source/host'
  executable('run-wbx', files('waterbox/run-wbx.c'),
    c_args: ['-I' + mb / 'source/host'],
    link_args: [mbhost / 'libminiboxhost.so'],
    build_rpath: mbhost)
endif
