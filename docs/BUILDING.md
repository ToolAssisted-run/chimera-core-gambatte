# Building

This core is built on Linux. The result, `core.wbx`, is one file that runs in
Chimera on Linux and on Windows.

## What is needed

- GCC 13 or newer (C and C++), meson, ninja, git, python3.
- A checkout of Chimera with miniBox built, with its C++ guest toolchain
  (Chimera's `AGENTS.md`, "Build"). Below, `<miniBox>` is its
  `extern/chimera-common-minibox` folder.

## Gambatte's source

Gambatte-Speedrun's emulator library is a submodule, `extern/gambatte-core`,
at a commit of its master branch:

```sh
git submodule update --init
```

The core compiles the files of `libgambatte/src` that upstream's
`libgambatte/SConstruct` lists, without zip support (`NO_ZIP`: Chimera hands
the core the game's own file) and without `src/cinterface.cpp`, upstream's
interface for other programs. `waterbox/cinterface.cpp` is the core's. There
are no patches to gambatte-core. If one is ever needed, it goes in
`patches/` as a numbered file: edit the files in `extern/gambatte-core`,
build and test, add the file to the list in `waterbox/make-patches.sh`, and
run that script. `waterbox/apply-patches.sh` lays the patches over the
submodule (the build runs it).

## The two flavors

- `native`: for the host. `run-native` is the reference the tests compare
  the sandbox with.
- `guest`: for miniBox's sandbox. This is `core.wbx`, and `run-wbx` runs it.

```sh
meson setup build/meson-native -Dminibox_dir=<miniBox> && ninja -C build/meson-native
waterbox/setup-guest.sh -m <miniBox> && ninja -C build/meson-guest
```

Both runners take a work folder that holds what the sandbox would see: the
game under its name, a boot ROM under its id (`dmg_boot.bin` and so on), a
file `slots` (`{"game":["game.gbc"]}`, with `"savedata":["game.sav"]` for
saved data) and a file `settings` (`{}`, or say
`{"model":"SGB2","bootRom":"off"}`). A Game Boy Link machine takes
`{"model":"LINK2"}` (or `LINK3`, `LINK4`), each linked Game Boy's console
as `linkModel1` to `linkModel4`, and P2's to P4's games and saved data in
the slots `game2` to `game4` and `savedata2` to `savedata4`. With no `model` the file's extension
chooses: `.gbc` the Game Boy Color, anything else the Game Boy.
`waterbox/gate-harness.h` lists the options they share (`--frames`,
`--report`, `--press`, `--inputs` (a file of the controls held on each
frame), `--watch` (bytes of the bus with each report), `--vid-out`,
`--av-stream`, `--dump-domain` (up to 8), `--savedata-out`,
`--list-panel`). `tests/bgra-to-png.py` turns a
`--vid-out` picture into a PNG.

## The package

```sh
waterbox/build-package.sh -r <chimera checkout>
```

builds the sandbox core and writes `build/Cores/gambatte.chimeraCore` in
that checkout. `tests/make-project.py` writes a project for a game, which
Chimera's `chimera-run --project` then plays.

## The tests

```sh
CHIMERA_ROOT=<chimera checkout> bash waterbox/run-gate.sh
TMPDIR=$PWD/build/t bash tests/run-frontend.sh --chimera-root <chimera checkout>
```

The first runs every test and says, for each, what was compared. Most need
no file of anyone's:

- `tests/make-testprog.py` writes a test cartridge of this repository's
  own, machine code written out in the script. It reads the buttons, counts
  frames, keeps a battery-backed memory, reads the cartridge's clock, makes
  a tone, and turns the screen off while Select is held. With `link=master`
  or `link=slave` it also talks through the link port, for the Game Boy
  Link tests.
  `tests/results.py` reads what it left in the work memory.
- `tests/hwtests.py` assembles gambatte-core's own hardware tests
  (`test/hwtests`) with upstream's own assembler (`test/qdgbas.py`), runs
  each through the core and judges it as upstream's runner
  (`test/testrunner.cpp`) does. `tests/hwtests-expected-failures.txt` lists
  the tests upstream's runner itself fails.

The boot ROM tests need the boot ROMs in `tests/roms-local/` (`dmg_boot.bin`,
`cgb_boot.bin`, `agb_boot.bin`, `sgb2_boot.bin`), and the game tests any
`.gb` or `.gbc` files in `tests/games-local/`. The Pokemon trade
(`tests/pokemon-trade.py`) needs Pokemon Gold (USA, Europe) there and
nothing else: `tests/pokemon-gold-opening.txt` plays it from power-on. Both folders are ignored by
git; the tests say SKIP without them.

The second script runs the package in Chimera's window under Mono on a
private Xvfb display; it needs Chimera's interface built (`dotnet build`).
It also runs two linked Game Boys in the window, and asks Chimera's own
helper (`Chimera.exe --suggest-settings`) what the core suggests for a link
project. Its game test needs a `.gbc` game in `tests/games-local/` and
`cgb_boot.bin` in `tests/roms-local/`.

Chimera's own checks of a package:

```sh
CHIMERA_CORES_DIR=<folder with the package> dotnet test source/gui/Chimera.Tests.Client.Common/Chimera.Tests.Client.Common.csproj -c Release --filter "FullyQualifiedName~InstalledCorePackagesTests|FullyQualifiedName~MnemonicUniquenessTests"
```

run in the Chimera checkout.

## What CI does

`.github/workflows/chimera.yml` builds Chimera and miniBox, the two
flavors and the package, runs both test scripts and Chimera's checks, and
publishes the package as this repository's release. A runner has no boot
ROM and no game: those tests are skipped there, and the workflow asks for
the others by name.
