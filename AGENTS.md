# AGENTS.md - Gambatte core for Chimera

This repository builds libgambatte, from Gambatte-Speedrun
(https://github.com/pokemon-speedrunning/gambatte-core), as a Game Boy and
Game Boy Color core for Chimera (https://github.com/ToolAssisted-run/chimera),
a frontend for tool-assisted speedruns. It produces one file,
`gambatte.chimeraCore`: libgambatte compiled to run inside Chimera's sandbox
(miniBox), a small program around it, and the declarations Chimera reads
(buttons, settings, machines, file types, firmware). Its machines are the
Game Boy, the Game Boy Color, the Game Boy Advance (Game Boy Color games),
the Super Game Boy 2, and Game Boy Link machines of two to four Game Boys
joined by link cables.

Read [docs/BUILDING.md](docs/BUILDING.md) before building and
[docs/PLAN.md](docs/PLAN.md) before changing how the core works: it is the
design log, with what was decided, why, and what was measured.

## Layout

- `extern/gambatte-core` - Gambatte-Speedrun at a commit of its master
  branch, a submodule. Only `libgambatte` is compiled into the core; its
  `test/hwtests` are run by the tests. Never edited in place: a change would
  be a numbered patch in `patches/` (there are none).
- `meson.build` - the build of both flavors.
- `waterbox/cinterface.cpp` - the program around libgambatte: it loads the
  game, the boot ROM and the saved data, sets the controls, runs the machine
  to the end of a picture, brings the sound down to 32,768 samples a second,
  and is what Chimera's engine calls. It also defines `time()`. For a link
  machine it runs up to four libgambatte machines a few samples at a time
  and passes the link ports' bytes between them (`link_frame`,
  `link_ports`).
- `waterbox/waterbox.config`, `file_slots.json`, `default_keybinds.json`,
  `package-licenses.json` - what the package declares. `licenses/` holds
  the licence texts.
- `waterbox/run-native.c`, `waterbox/run-wbx.c` - the native runner and the
  sandbox runner. They take the same options and print the same lines
  (`waterbox/gate-harness.h` is the part they share).
- `waterbox/*.sh` - `apply-patches.sh`, `make-patches.sh`, `setup-guest.sh`
  (configures the sandbox build), `build-package.sh`, `run-gate.sh` (the
  test script).
- `tests/` - `make-testprog.py` (the test cartridge) and `results.py`
  (reads what it left), `hwtests.py` and `hwtests-expected-failures.txt`
  (upstream's hardware tests through the core), `pokemon-trade.py` and
  `pokemon-gold-opening.txt` (a Pokemon trade through the link cable, from
  inputs of this repository's own), `sgb-screen.py`,
  `check-key-names.py`, `run-frontend.sh` (the window test, with its
  helpers), `make-project.py` (writes a project), `bgra-to-png.py` (a
  picture the runners leave, to look at). `roms-local/` and `games-local/`
  hold boot ROMs and games of yours and are ignored by git.
- `build/` - every build output, ignored by git.

## Build and test

```sh
git submodule update --init
meson setup build/meson-native -Dminibox_dir=<miniBox> && ninja -C build/meson-native
waterbox/setup-guest.sh -m <miniBox> && ninja -C build/meson-guest
waterbox/build-package.sh -r <chimera checkout>
CHIMERA_ROOT=<chimera checkout> bash waterbox/run-gate.sh
TMPDIR=$PWD/build/t bash tests/run-frontend.sh --chimera-root <chimera checkout>
```

Run both test scripts before every commit that touches what they cover, and
say in the commit what ran, with the numbers. A new test is broken on
purpose once and seen to fail.

## Rules of the repository

- **libgambatte's machine, as upstream has it.** The core changes nothing
  in the emulation. `upstream:hwtests` runs upstream's own hardware tests
  through the core; a result other than upstream's runner gives is a bug to
  find, not to accept.
- **The machine's own time.** A cartridge's clock counts the machine's
  cycles (`setTimeMode(true)`), and `time()` is defined in the core to say
  no time passes: never the computer's clock.
- **The same numbers in both builds.** The sound is brought down with
  integer arithmetic; the Super Game Boy's sound chip is built with
  `-ffp-contract=off`.
- **The machine's state is its memory.** A savestate is the sandbox's copy
  of the core's memory: the picture and the sound filter's sums included.
- **The link machines are BizHawk's**: which Game Boys are joined, and the
  link calls, are its Gambatte Link's (read for ideas, not copied): the
  same wiring and the same steps of 4 samples.
- **The controls are in two places that must agree**: the tables in
  `cinterface.cpp` and the `input` of each machine in `waterbox.config`.
  The test `package:controls` compares them; `package:key-names` checks the
  default bindings name keys Chimera knows.
- **No boot ROM or game is committed**, uploaded to CI or attached to an
  issue.
- **Nothing from BizHawk** (owner's rule, 2026-10-15): no movie, save or
  other file of BizHawk's, not even for a test. Test inputs are this
  repository's own: raw input files or Chimera projects.
- **Say what was not tried.** A game that was not run has not been shown to
  work.
- **Write in plain language**: commit messages, documents, comments and
  setting descriptions are read by people who have never seen this
  repository. `.md` files are ASCII.
- **Commits**: `type(scope): what changed`; a short body with what changed,
  why, and how it was tested, with the numbers. Stage explicit paths. No
  amending, no force push.

## What is the owner's to decide

Publishing, the roster row in Chimera, moving to another gambatte-core
commit, and anything a licence does not plainly allow.
