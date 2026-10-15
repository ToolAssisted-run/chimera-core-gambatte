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

- The link cable: done, see the next entry.
- The Game Boy Printer, the Game Boy Camera's picture, the infrared port's
  other end, and the infrared remote: libgambatte has calls
  for them; nothing answers them.
- Colour correction, and BizHawk's frame lengths other than vblank-driven
  (equal-length frames, frames of a length set per frame).

## 2026-10-15: Game Boy Link (user-decided)

The owner's go-ahead: "go ahead with the link feature". BizHawk's Gambatte
Link was read for how it does it; the code here is this repository's.

### Machines, settings, files

- Model `LINK2`, `LINK3` and `LINK4` are three more machines of the
  package, system GBL ("Game Boy Link", BizHawk's id): two, three or four
  Game Boys joined by link cables, each with its own game.
- Each linked Game Boy's console is a setting of its own, `linkModel1` to
  `linkModel4` (GB, GBC or GBA), shown only for the machines that have that
  Game Boy. A Super Game Boy cannot be linked (BizHawk allows it; its
  border and four pads in a row of screens are left out here), and is
  refused if asked for.
- The New Project window gets the consoles suggested: the core answers
  `SuggestSettings` with the Game Boy Color for a `.gbc` game and the Game
  Boy otherwise. Chimera asks before the settings are known, with the
  files only, so the core takes a project with a game for P2 to be a link
  project (a first version looked at the Model setting and answered nothing
  in the real wizard; the window test now asks through Chimera's own
  helper, `--suggest-settings`).
- The slots `game2` to `game4` and `savedata2` to `savedata4` hold the other
  Game Boys' games and saved data, shown only for the machines that have
  them. Boot ROMs are needed for every console a linked Game Boy is; the
  firmware conditions name each `linkModel`.

### How the Game Boys run together

As BizHawk's link machine: every Game Boy runs the whole frame, 35,112 of
its samples, 4 samples (8 cycles) at a time while the cable is in, and
between steps each joined pair's link ports are looked at: when one has
clocked a byte out, the two bytes are swapped (libgambatte's `linkStatus`;
the infrared light the same way). A Game Boy that ran past a step keeps
its lead; one that ran past the frame's end starts the next frame with
those samples. With the cable out, each runs the frame in one go and its
link port is unplugged (a byte sent on its own clock brings in 255).

So a link machine's frames are all the same length, unlike one console's,
which end with each picture. A Game Boy's picture is copied into its place
in the row whenever it finishes one. The sound of all of them is mixed,
each at its share.

Which are joined, as in BizHawk: two, P1 and P2. Three: P1 and P2;
shifted, P2 and P3; spaced out, P1 and P3. Four: P1 with P2 and P3 with
P4; shifted, P2 with P3 and P4 with P1; spaced out, P1 with P3 and P2 with
P4. Toggle Link Shift and Toggle Link Spacing change it when pressed. In
BizHawk's code (GambatteLink.IEmulator.cs) the shift is set, on the line
after its toggle, to whether Toggle Link Shift is held, so there it follows
the button rather than toggling; here it toggles, as the spacing and the
connection do in both.

The controls: a pad and a Power for each Game Boy, as BizHawk's link
machine has them (Power restarts that Game Boy alone). A frame is a lag
frame when no Game Boy read its buttons. Memory areas and the system bus
are each Game Boy's ("P2 WRAM", "P2 System Bus"); Export Save Data writes
"P2 <game>.sav" for each one with a battery.

The core now also exports the system bus as Chimera finds one
(`GetBusCount`, `GetBusName`, `GetBusSize`, `GetBusWritable`, `PeekBus`);
the single console only had `ReadBus`, which Chimera does not look at on
its own, so the bus was not shown before.

### Tests

The test program got a link mode (`tests/make-testprog.py link=master` or
`link=slave`): a master sends its frame count every frame on its own clock;
a slave answers on the master's clock with its own count, the top bit set.
Each keeps the last byte that came in and counts transfers.

`waterbox/run-gate.sh` is now 54 tests (38 before); the new ones:

- `link:exchange`: two Game Boys pass a byte each way every frame, 300
  transfers in 300 frames, each getting the other's count; 320x144.
- `link:unplugged`: the cable pulled at frame 100 stops the transfers (100)
  and the master gets 255; put back at 200, they go on.
- `link:three` and `link:four`: which are joined, with the shift and the
  spacing (counts like 44 1, 100 201, 100 1 for three; for four, the
  spaced master gets the other master's count, without the top bit).
- `link:controls`: each Game Boy's pad reaches it alone, and P2's Power
  restarts P2 alone (P1 300 frames, P2 151 since).
- `link:models`: a Game Boy Color as P1 draws another picture than a Game
  Boy there, and P2's is the same.
- `link:sound`: 329,175 samples in 600 frames, exactly (equal frames), and
  P2's sound is in the mix.
- `link:lag`, `link:save-data`, `link:suggest`, `refused:link`.
- `link:sandbox`: native, sandbox, a state saved and loaded every frame,
  and a new sandbox half way, for two (with the cable pulled and put back
  and a Power) and four (with the shift and spacing changed);
  `link:state-rewind`.
- `link:boot` (local): a Game Boy and a Game Boy Color linked, each with its
  boot ROM, exchange after them.
- `link:game` (local): two Pokemon Golds linked, 2,000 frames from the boot
  ROM with both pads used; native and sandbox the same.
- `engine:link`: a link project with P2's A and the cable pulled plays in
  Chimera's headless runner to the sandbox runner's memory.

Result: 54 passed, 0 failed, 0 skipped. `tests/run-frontend.sh`: 6 passed
(new: `window:link`, two linked Game Boys in the window, and
`window:suggest`). Chimera's package checks: 18 passed. Windows: a link
project of the test programs (cable pulled, a Power) and two Pokemon Golds
linked for 3,000 frames end with the same memory as on Linux.

Broken on purpose, each seen to fail: a byte passed one way only
(`link:exchange`, `link:three`, `link:four`, `engine:link`); the cable
toggle doing nothing (`link:unplugged`); the shifted three wired as spaced
(`link:three`); every Game Boy reading P1's pad (`link:controls`);
`linkModel` ignored (`link:models`, `refused:link`); P2's saved data from
P1's slot (`link:save-data`); no suggestion (`link:suggest`); only P1's
sound mixed (`link:sound`); only P1's reads counted (`link:lag`).

### Not done or not proven

- No game's own link feature had been played through the cable here; see
  the next entry for a Pokemon trade.
- A Super Game Boy cannot be linked.
- BizHawk's sound choices for the link machine (which Game Boy is heard on
  which side, each one muted or not) are not offered; all are mixed.

## 2026-10-15: a Pokemon trade through the cable (user-decided)

The owner asked for a scripted Pokemon trade. `tests/pokemon-trade.py`
writes its inputs and checks the result; the test `link:trade` runs it. It
needs the owner's Pokemon Gold and nothing else.

- **Nothing from BizHawk** (owner's rule): a first version brought the game
  to a save with a BizHawk movie of the owner's; the owner ruled that out,
  for tests too ("we should never use anything from bizhawk, not even for
  testing. Use chimeraProject or raw input files"). The movie and all made
  from it were deleted.
- **A saved game of our own.** The Cable Club opens with the Pokedex, and
  the game will not trade away a player's only Pokemon
  (`CheckAnyOtherAliveMonsForTrade` in pret's disassembly), so each game
  needs two. `tests/pokemon-gold-opening.txt` (61,086 frames, a run of
  frames a line) plays it from power-on, on a Game Boy Color with no boot
  ROM: the options (text Fast, battle scene Off, battle style Set), a new
  game as GOLD, Cyndaquil from Elm, the Pokedex from Mr. Pokemon and Oak,
  the rival battle in Cherrygrove, the Mystery Egg to Elm, five Poke Balls,
  a Pidgey caught on Route 29 (the second ball), and a save in Cherrygrove
  City's Pokemon Center. It was written by playing through the core: paths
  planned on pret's maps and collision tables, the position read back after
  each stretch, wild battles run from. The save comes out the same natively
  and in the sandbox.
- **The link machine needs its own inputs**: a link machine's frames are
  all 35,112 samples, a single console's end with each picture. The two
  linked games start from the save (Continue) with the script's steps:
  left onto the stairs, the Trade Center's attendant, her save, the link,
  the seats, the trade screen, TRADE, yes.
- **Two identical games do not connect.** From the same save on the same
  frame, the two games are the same machine to the cycle: both send at the
  same moment on their own clock, the swap of the two bytes is done twice
  (once for each), each gets its own byte back, and the Cable Club gives
  up with "The link has been closed because of inactivity". Two people
  never press on the same frame: P2's inputs come 23 frames after P1's, and
  the link connects.
- **The result**: P1's party goes from Cyndaquil and Pidgey to Pidgey and
  Pidgey, P2's to Cyndaquil and Cyndaquil (party at DA22, Data Crystal's
  map of Pokemon Gold's memory). The trade's 8,514 frames give the same 86
  digests natively and in the sandbox. With P2's delay at 0, broken on
  purpose, the parties stay as they were and the test fails.

The runners got three options for this, kept for any test: `--inputs` (a
file of the controls held on each frame), `--watch` (bytes of the bus
printed with each report) and up to 8 `--dump-domain`.

Test script: 55 passed, 0 failed, 0 skipped. The game stays in the ignored
local folder; on CI the test is skipped.
