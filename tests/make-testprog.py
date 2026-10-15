#!/usr/bin/env python3
"""Writes the core's test program: a Game Boy cartridge of this
repository's own, so the tests need no game of anyone's.

usage: make-testprog.py OUT.gb|OUT.gbc [nopad]

The cartridge is 32 KB of program with an MBC3 chip, 8 KB of battery-backed
memory and the MBC3's clock, the kind of cartridge Pokemon Gold is. The
program is machine code written out below, one instruction a line. Once a
frame, at the start of the screen's vertical blank, it:

  - reads the eight buttons and keeps them at C000 (now) and C001 (every
    button pressed so far): Down, Up, Left, Right in the top four bits,
    Start, Select, B, A in the bottom four;
  - counts frames at C002 (16 bits, low byte first);
  - reads the cartridge clock's seconds into C004;
  - copies C000, C001 and C002 into the cartridge's memory at A000;
  - turns the screen off while Select is held, and on again after; with
    the screen off it counts a frame's time in a loop instead;
  - changes the background's colour (the Game Boy's BGP register and the
    Game Boy Color's first palette) and the pitch of a square wave on sound
    channel 1 from the frame count and the buttons.

At power-on it sets C000 to C004 to zero, adds one to the cartridge's memory at A010 and copies it to
C005: how many times this cartridge was started, which shows whether saved
data was loaded.

With "nopad" the program never reads the buttons (C000 and C001 stay
zero): every frame of it is a lag frame.

A file ending in .gbc says in its header that it works on the Game Boy
Color (header byte 0x143 = 0x80); a .gb file says nothing (0x00). The
program is the same.
"""
import sys

# The 48 bytes every cartridge carries at 0x104, which the boot ROM checks
# before it starts a game; without them it stops at the logo.
LOGO = bytes.fromhex(
    "ceed6666cc0d000b03730083000c000d0008111f8889000edccc6ee6dddd"
    "d999bbbb67636e0eecccdddc999fbbb9333e")


def assemble(lines, origin):
    """Two passes over (bytes or ('jr', cond, label) or ('jp', label) or
    label-string) items."""
    labels = {}
    for pass_ in (0, 1):
        out = bytearray()
        for item in lines:
            pc = origin + len(out)
            if isinstance(item, str):
                labels[item] = pc
            elif isinstance(item, tuple) and item[0] == "jr":
                op = {"": 0x18, "nz": 0x20, "z": 0x28, "nc": 0x30, "c": 0x38}[item[1]]
                target = labels.get(item[2], pc + 2) if pass_ else pc + 2
                rel = target - (pc + 2)
                assert -128 <= rel <= 127, item
                out += bytes([op, rel & 0xFF])
            elif isinstance(item, tuple) and item[0] == "jp":
                target = labels.get(item[1], 0) if pass_ else 0
                out += bytes([0xC3, target & 0xFF, target >> 8])
            else:
                out += bytes(item)
    return bytes(out)


def ld_a_mem(addr):  # ld a,(nn)
    return [0xFA, addr & 0xFF, addr >> 8]


def ld_mem_a(addr):  # ld (nn),a
    return [0xEA, addr & 0xFF, addr >> 8]


def ldh_w(port, value):  # ld a,value ; ldh (port),a
    return [0x3E, value, 0xE0, port]


START = [
    [0xF3],                          # di
    [0x31, 0xFE, 0xFF],              # ld sp,FFFE
    [0xAF],                          # xor a: the work memory's counters start at
    ld_mem_a(0xC000), ld_mem_a(0xC001), ld_mem_a(0xC002),  # zero (it is not
    ld_mem_a(0xC003), ld_mem_a(0xC004),                    # cleared at power-on)
    [0x3E, 0x0A], ld_mem_a(0x0000),  # the cartridge's memory on
    [0xAF], ld_mem_a(0x4000),        # its first bank
    [0x21, 0x10, 0xA0],              # ld hl,A010
    [0x34],                          # inc (hl): one more start
    [0x7E], ld_mem_a(0xC005),        # ld a,(hl) ; ld (C005),a
    ldh_w(0x26, 0x80),               # sound on
    ldh_w(0x24, 0x77),               # both sides at full volume
    ldh_w(0x25, 0xFF),               # every channel to both sides
    ldh_w(0x11, 0x80),               # channel 1: half-and-half wave
    ldh_w(0x12, 0xF0),               # full volume, no fading
    ldh_w(0x13, 0x00),
    ldh_w(0x14, 0x86),               # start it
    ldh_w(0x00, 0x30),               # no button line selected: the boot ROM
                                     # leaves both, and the machine would read
                                     # them for the program every frame
    "loop",
    [0xF0, 0x40],                    # ldh a,(LCDC)
    [0xCB, 0x7F],                    # bit 7,a: the screen on?
    ("jr", "z", "dark"),
    "vblank",
    [0xF0, 0x44],                    # ldh a,(LY)
    [0xFE, 0x90],                    # cp 144
    ("jr", "nz", "vblank"),
    ("jr", "", "go"),
    # with the screen off there is no vertical blank: a frame's time,
    # 2,508 turns of 28 cycles
    "dark",
    [0x01, 0xCC, 0x09],              # ld bc,2508
    "spin",
    [0x0B], [0x78], [0xB1],          # dec bc ; ld a,b ; or c
    ("jr", "nz", "spin"),
    "go",
]

# the buttons into C000, and every button so far into C001
READ_PAD = [
    # the direction keys: select them, read four times to let them settle
    ldh_w(0x00, 0x20),
    [0xF0, 0x00], [0xF0, 0x00], [0xF0, 0x00], [0xF0, 0x00],
    [0x2F],                          # cpl: pressed is 1
    [0xE6, 0x0F],                    # and 0F
    [0xCB, 0x37],                    # swap a: to the top four bits
    [0x47],                          # ld b,a
    ldh_w(0x00, 0x10),               # the buttons
    [0xF0, 0x00], [0xF0, 0x00], [0xF0, 0x00], [0xF0, 0x00],
    [0x2F], [0xE6, 0x0F],
    [0xB0],                          # or b
    ld_mem_a(0xC000),
    [0x47],                          # ld b,a
    ld_a_mem(0xC001), [0xB0], ld_mem_a(0xC001),
    ldh_w(0x00, 0x30),               # neither
]

REST = [
    # the frame count
    [0x21, 0x02, 0xC0],              # ld hl,C002
    [0x34],                          # inc (hl)
    ("jr", "nz", "counted"),
    [0x23], [0x34],                  # inc hl ; inc (hl)
    "counted",
    # into the cartridge's memory
    ld_a_mem(0xC000), ld_mem_a(0xA000),
    ld_a_mem(0xC001), ld_mem_a(0xA001),
    ld_a_mem(0xC002), ld_mem_a(0xA002),
    # the colour: the frame count XOR the buttons
    ld_a_mem(0xC002), [0x47],        # ld b,a
    ld_a_mem(0xC000), [0xA8],        # xor b
    [0xE0, 0x47],                    # ldh (BGP),a
    [0x4F],                          # ld c,a
    ldh_w(0x68, 0x80),               # Game Boy Color: palette 0, colour 0, counting on
    [0x79], [0xE0, 0x69],            # ld a,c ; ldh (BCPD),a
    [0x78], [0xE0, 0x69],            # ld a,b ; ldh (BCPD),a
    # the pitch: the buttons, without starting the note again
    ld_a_mem(0xC000), [0xE0, 0x13],
    ldh_w(0x14, 0x06),
    # the clock's seconds: register 08, latched by writing 0 then 1 to 6000
    [0x3E, 0x08], ld_mem_a(0x4000),
    [0xAF], ld_mem_a(0x6000),
    [0x3C], ld_mem_a(0x6000),        # inc a
    ld_a_mem(0xA000), ld_mem_a(0xC004),
    [0xAF], ld_mem_a(0x4000),        # back to memory bank 0
    # Select held turns the screen off (it is the vertical blank: the one
    # time that may be done); let go, on again
    ld_a_mem(0xC000), [0xE6, 0x04],  # and 04
    ("jr", "z", "lit"),
    ldh_w(0x40, 0x11),
    ("jp", "loop"),
    "lit",
    ldh_w(0x40, 0x91),
    # wait for the vertical blank to end, then the next one
    "leave",
    [0xF0, 0x44], [0xFE, 0x90],
    ("jr", "z", "leave"),
    ("jp", "loop"),
]


def main():
    out = sys.argv[1]
    rom = bytearray(32768)
    rom[0x100:0x104] = bytes([0x00, 0xC3, 0x50, 0x01])  # nop ; jp 0150
    rom[0x104:0x134] = LOGO
    title = b"CHIMERA TEST"
    rom[0x134:0x134 + len(title)] = title
    rom[0x143] = 0x80 if out.lower().endswith(".gbc") else 0x00
    rom[0x147] = 0x10  # MBC3 + clock + memory + battery
    rom[0x148] = 0x00  # 32 KB
    rom[0x149] = 0x02  # 8 KB of memory
    rom[0x14A] = 0x01
    rom[0x14B] = 0x00
    nopad = "nopad" in sys.argv[2:]
    code = assemble(START + ([] if nopad else READ_PAD) + REST, 0x150)
    rom[0x150:0x150 + len(code)] = code
    # the header's checksum, which the boot ROM checks, and the whole
    # cartridge's, which nothing checks
    c = 0
    for b in rom[0x134:0x14D]:
        c = (c - b - 1) & 0xFF
    rom[0x14D] = c
    g = sum(rom) & 0xFFFF
    rom[0x14E:0x150] = bytes([g >> 8, g & 0xFF])
    open(out, "wb").write(rom)


if __name__ == "__main__":
    main()
