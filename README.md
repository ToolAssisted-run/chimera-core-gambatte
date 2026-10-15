# chimera-core-gambatte

A core for [Chimera](https://github.com/ToolAssisted-run/chimera), the
frontend for tool-assisted speedruns, for the **Game Boy** and the **Game Boy
Color**. It is built on libgambatte from
[Gambatte-Speedrun](https://github.com/pokemon-speedrunning/gambatte-core),
the Game Boy emulator speedrunners use, and produces one file,
`gambatte.chimeraCore`, that goes in Chimera's `Cores` folder.

## Status

New, and not published yet.

- Four consoles: the Game Boy, the Game Boy Color, a Game Boy Advance
  running Game Boy Color games, and the Super Game Boy 2.
- Game Boy Link: two, three or four of them joined by link cables, each
  with its own game, as BizHawk's Gambatte Link does (trading and battling
  in Pokemon, for instance).
- Gambatte's own 3,000-odd hardware tests give, through the core, exactly
  the results upstream's own test runner gives.
- Tried: Pokemon Gold, Prince of Persia, Super Mario Bros. Deluxe, Kirby's
  Star Stacker and Bugs Bunny Crazy Castle II, each for 2,000 frames from
  the boot ROM, in the sandbox and natively, and Pokemon Gold in Chimera's
  window and on Windows. No game was played through.
- The link cable was tried with test programs of this repository's that
  talk through it, and two Pokemon Golds were linked for 3,000 frames; no
  game's own trade or battle over the cable has been played yet.
- Not there: the Game Boy Printer, the Game Boy Camera's picture
  and the infrared port's other end.

## What you bring

- A game: a `.gb` or `.gbc` file; for a Game Boy Link machine, one for each
  Game Boy (the same game twice is fine).
- The boot ROM of the console you choose, which is Nintendo's: `dmg_boot.bin`
  (Game Boy), `cgb_boot.bin` (Game Boy Color), `agb_boot.bin` (Game Boy
  Advance) or `sgb2_boot.bin` (Super Game Boy 2). With the Boot ROM setting
  off none is needed: the game starts at once, in the state the boot ROM
  would have left.

## Buttons and settings

- **Buttons**: Up, Down, Left, Right, Start, Select, B and A. The Super Game
  Boy has four controllers, P1 to P4. **Power** turns the console off and
  on again.
- **Model**: which console, chosen with the machine in the New Project
  window: one of the four, or a Game Boy Link machine of two, three or four.
- **P1 Game Boy** to **P4 Game Boy**: on a Game Boy Link machine, which
  console each is (Game Boy, Game Boy Color or Game Boy Advance). The New
  Project window picks the Game Boy Color for a `.gbc` game.
- **On a Game Boy Link machine**: a pad and a Power for each Game Boy, and
  Toggle Link Connection, which pulls the cables out and puts them back.
  With three or four Game Boys, Toggle Link Shift and Toggle Link Spacing
  change which are joined, as in BizHawk. The screens are side by side.
- **Boot ROM**: on (the console's own start-up, with the logo) or off.

The cartridge's clock (Pokemon Gold, Silver and Crystal have one) counts the
console's own time, so a movie replays the same.

Emulator > Export Save Data writes the cartridge's battery memory, with the
clock's registers after it, as Gambatte writes it (for each Game Boy of a
link machine, "P2 <game>.sav" and so on); a project can start from one.

RAM Watch and RAM Search read the work memory (WRAM), the video memory, the
cartridge's memory, the sprite table (OAM), the high memory (HRAM), the
cartridge's ROM, and the 64 KB the processor sees (the system bus); on a
link machine, each Game Boy's ("P2 WRAM").

## Building and testing

[docs/BUILDING.md](docs/BUILDING.md) has every step. [docs/PLAN.md](docs/PLAN.md)
is the design log: what was decided, why, and what was measured.

## Licence

This repository's own code is MIT. libgambatte is under the GNU GPL 2, and
the package, which holds it, is under the GPL 2. See [LICENSE](LICENSE).
