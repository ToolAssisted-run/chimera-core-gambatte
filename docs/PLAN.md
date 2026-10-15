# Design log

Dated entries: what was decided, why, and what was measured.

## 2026-10-15: Gambatte on its own, the link cable after (user-decided)

The owner asked for BizHawk's Game Boy link feature, where two to four Game
Boys play together through the link cable (Pokemon trades and battles). In
BizHawk that is Gambatte Link: several Gambatte machines in one, stepped
together, with the bytes of the link port passed between them. The owner
decided: first Gambatte as a core of its own, then its link feature.

### Which Gambatte

Gambatte-Speedrun (`pokemon-speedrunning/gambatte-core`), at its master
branch's commit `5a41a68c` (2026-04-16), the latest. It is the fork the
Pokemon speedrunning community keeps, and the one BizHawk builds (BizHawk's
copy is an older commit of it, `d49b8953`). Over Sindre Aamas's original it
adds the Super Game Boy 2, Game Boy games on the Game Boy Color, a clock
that counts the machine's cycles, and the link and infrared calls BizHawk's
link machine uses. No work was taken from BizHawk: its source was read for
how it calls libgambatte (frames, sound, lag, the Super Game Boy's pads).

- **Licence**: the GNU GPL 2 (`COPYING`). Its Super Game Boy sound chip is
  Shay Green's snes_spc, under the LGPL 2.1 or later; its CRC-32 is Gary S.
  Brown's, free without restriction. Five small files carry no header and
  fall under `COPYING`. The package is under the GPL 2.
- **One thread, one clock**: libgambatte has no thread. Its only clock
  reads are `std::time` for a cartridge clock in real-time mode, and the
  time it writes beside a clock's registers in saved data (below).
- **The picture**: libgambatte's own, 160x144 (256x224 for the Super Game
  Boy, with the border).
- **No recompiler.**

### The program around it

`waterbox/cinterface.cpp`, in place of upstream's own programs:

- **A frame** is one `runFor` of 35,112 samples (70,224 cycles): it ends at
  the end of a picture, or after a frame's time when the screen is off. That
  is BizHawk's default ("vblank-driven frames"), so a frame here is a frame
  there. The finished picture is copied out only when one was finished;
  with the screen off libgambatte finishes a white one every frame, as the
  console shows.
- **The sound**: the machine gives 2,097,152 stereo samples a second. The
  Super Game Boy's SNES sound chip adds one every 65 of those, and a HuC3
  cartridge's speaker one for each; they are added on the machine's
  timeline as BizHawk adds them (the Game Boy's at half its level). The sum
  is brought down to 32,768 samples a second, one in 64, through a
  third-order CIC filter: three running sums, one sample in 64 kept, three
  differences. It is integer arithmetic, so every computer gives the same
  samples, and it removes what would otherwise fold back into the audible
  range. Measured: 329,139 samples in 600 frames (600 of 35,112 would be
  329,175; the first frame is cut short where the machine starts).
- **The buttons**: libgambatte asks for them through a callback whenever
  the program reads them, and once at the start of each run while a button
  line is selected. A frame in which it never asked is a lag frame: what
  BizHawk counts. For the Super Game Boy, Left with Right and Up with Down
  read as neither, as BizHawk has it (the SNES software never passes them).
- **Power** restarts the console (`reset`), with the same start-up wait as
  at power-on.
- **The memory areas**: ROM (read only), VRAM, WRAM, CartRAM, OAM and HRAM,
  libgambatte's own; and the system bus, read with libgambatte's peek,
  which changes nothing in the machine.

### The models, as machines (Chimera's machine declaration)

The Model setting is GB, GBC, GBA or SGB2, and each is one of the package's
machines: Game Boy (system GB), Game Boy Color (GBC), Game Boy Advance for
Game Boy Color games (system GBC, as BizHawk files it) and Super Game Boy 2
(SGB). Chimera chooses the machine for a game opened with no project from
its extension (`.gb`, `.gbc`, `.sgb`), and the New Project window asks for
it. A first version had a Model 'auto' that went by the extension inside
the core; Chimera then took the package for a Game Boy core only, and
refused a `.gbc` project ("not a valid GBC core"). With no Model at all (the
test runners can give none) the core still goes by the extension.

The Game Boy Advance draws the same picture as the Game Boy Color here.
libgambatte gives the raw colours of the Game Boy Color and a grey Game Boy,
and the core applies no colour correction; BizHawk applies one of its own,
another for the Game Boy Advance.

### The boot ROMs (default user-decided, 2026-10-15)

Setting Boot ROM: on (the default, the owner's choice) runs the console's own start-up, which
needs that console's boot ROM as firmware: `dmg_boot.bin`, `cgb_boot.bin`,
`agb_boot.bin`, `sgb2_boot.bin`, each needed only for its Model. Off starts
the game in libgambatte's guess at the state the boot ROM leaves, as
BizHawk's "Use official Nintendo BootROM" unticked does. BizHawk's default
is off; Gambatte-Speedrun exists for timing that matches a console, which
needs the boot ROM. Measured, the test cartridge starts at frame 335 after
the Game Boy's boot ROM, 187 after the Game Boy Color's, 201 after the Game
Boy Advance's, and 149 after the Super Game Boy 2's (120 with no boot ROM:
the SNES starts the Game Boy late).

### The cartridge's clock and saved data

libgambatte's clock counts the machine's cycles (`setTimeMode(true)`),
never the computer's: 10 seconds after 600 frames, 20 after 1,200. Saved
data with a clock has, after the cartridge's memory, the time it was
written and the clock's registers; on loading, libgambatte moves the clock
on by the time that passed. The core defines `time()` to say 0, so no time
passes between writing and loading, and the same saved data loads the same
everywhere. A shorter file (another emulator's `.sav` with no clock) leaves
the rest at zero.

Export Save Data writes `<game>.sav`: 8,214 bytes for the test cartridge
(8 KB of memory and the clock's 22 bytes). A project with it as saved data
starts from it: the cartridge's count of starts goes on, and its clock from
the 5 seconds it had.

### What the tests compare

`waterbox/run-gate.sh`, 38 tests:

- The source is the pinned commit; the package is not older than its
  sources; each machine declares the core's 33 buttons; the default key
  bindings name keys Chimera knows.
- The test cartridge (`tests/make-testprog.py`, machine code written out in
  the script): it runs; each of the 8 buttons is its own bit; lag frames (0
  in 300 when the program reads the buttons, 299 when it never does); the
  screen off while Select is held (a white picture, the program still
  counting); the clock; the sound (its count, and its pitch following the
  buttons); Power; the saved data, out and back in; the system bus.
- The consoles: a `.gbc` with no Model on the Game Boy Color; GBA, SGB2 and
  a Game Boy game on the Game Boy Color each run, and draw pictures of their
  own; the Super Game Boy's picture has the screen 48 pixels in and 40 down,
  and all four pads are live.
- Native against sandbox, with presses and Power: the same 60 digests of
  memory, picture and sound; a state saved and loaded every frame (1.6 MB a
  state); the machine moved to a new sandbox half way, with the screen off
  at that frame; a state kept at frame 800, screen off, and loaded at 1200.
  The same on the Game Boy Color and the Super Game Boy.
- No game, a file too short to be a game, and Boot ROM on with no boot ROM
  are refused, and say why.
- `upstream:hwtests`: gambatte-core's own hardware tests (3,524 programs in
  `test/hwtests`), assembled with upstream's own assembler and run through
  the core with no boot ROM, judged as upstream's runner judges them: Game
  Boy 1,740 of 1,740, Game Boy Color 3,154 of 3,154, Game Boy Advance 3,080
  of 3,154. Upstream's own runner, built from the same commit, fails the
  same 74 Game Boy Advance tests and no others; upstream marks their
  expected results as not yet real for that console. The tests judged by a
  picture file (upstream's .png files) are left out: upstream's runner sets
  palettes of its own for them. One test in 25 runs through the sandbox
  too, with the native build's results.
- With the boot ROMs (`tests/roms-local`): each of the four starts the
  program at least 20 frames later than with none, native and sandbox the
  same.
- Games of yours (`tests/games-local`): each runs 2,000 frames from the
  boot ROM with Start and A pressed, native and sandbox the same.
- Chimera's headless runner plays a project of the test cartridge to the
  sandbox runner's memory.

`tests/run-frontend.sh`, 4 tests, in Chimera's window: the test cartridge's
memory against the headless runner's; the package's keys; a project whose
boot ROM is missing is refused with the file named; Pokemon Gold with the
Game Boy Color's boot ROM from Chimera's firmware, 1,500 frames, to the
headless runner's memory.

Chimera's checks of the package: 18 passed.

### Broken on purpose

Each new test was seen to fail on a core broken for it:

- A and B swapped: `prog:buttons` and `engine:project` failed.
- The clock on the computer's time: `prog:clock` failed.
- One sample in 63 instead of 64: `prog:sound` failed.
- Power doing nothing: `prog:power` failed.
- Every frame said to read the buttons: `prog:lag` failed.
- Saved data not loaded: `prog:save-data` failed.
- The bus reading zeros: `prog:bus` failed.
- The Super Game Boy's picture without its border step:
  `consoles:sgb-screen` failed.
- Game Boy Color mode not set for GBC: `consoles:no-model`,
  `consoles:models` and `upstream:hwtests` failed.
- The boot ROM not loaded: all four `boot:` tests failed (the Super Game
  Boy's only once it was measured against its own start with no boot ROM;
  the first version of that test passed on the broken core).
- A black picture on a frame with none finished: `prog:screen-off` failed.

Not caught, and why: the picture kept in memory a state does not carry.
libgambatte finishes a picture every frame, white with the screen off, so
no frame shows an old one; the picture stays in the state's memory anyway.

### Where it was run

- Linux: everything above.
- Windows: Chimera's headless runner (the mingw build) played the test
  cartridge's project and Pokemon Gold with the boot ROM for 3,000 frames
  to the same memory as on Linux.
- Games: Pokemon Gold, Prince of Persia (Game Boy Color), Super Mario Bros.
  Deluxe, Kirby's Star Stacker (Japanese) and Bugs Bunny Crazy Castle II,
  2,000 frames each from the boot ROM. None was played through.

### Not done

- The link cable: next, as the owner decided.
- The Game Boy Printer, the Game Boy Camera's picture, the infrared port's
  other end, and the infrared remote: libgambatte has calls
  for them; nothing answers them.
- Colour correction, and BizHawk's frame lengths other than vblank-driven
  (equal-length frames, frames of a length set per frame).
