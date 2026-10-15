#!/usr/bin/env python3
"""A Pokemon trade through the link cable: two Pokemon Gold games, linked,
trade a Pokemon each way in the Cable Club's Trade Center.

The games need a saved game past the Pokedex (the Cable Club opens with it)
with two Pokemon (the game will not trade away a player's only one),
standing in a Pokemon Center. tests/pokemon-gold-opening.txt is how to get
there from power-on: inputs of this repository's own, played on one Game
Boy Color with no boot ROM, from a new game to a save in Cherrygrove City's
Pokemon Center with Cyndaquil and a Pidgey. The two linked games then start
from that save (Continue), walk upstairs, talk to the Trade Center's
attendant, save again, go in, sit down at the console and trade: P1 gives
Cyndaquil, P2 gives Pidgey.

The second Game Boy's inputs come 23 frames after the first's. Two games
started from the same save on the same frame are the same machine to the
cycle: both offer to drive the link's clock at the same moment, each byte
crosses the other in the cable, and the Cable Club gives up ("The link has
been closed because of inactivity"). People never press A on the same
frame; 23 frames apart is enough.

usage:
  pokemon-trade.py save-inputs <out>         the opening, a line a frame
  pokemon-trade.py trade-inputs <out>        the linked games' inputs
  pokemon-trade.py check <P1 WRAM> <P2 WRAM> the parties after the trade

Inputs files are the runners' --inputs format: a line a frame, the controls
held, separated by '|'.
"""
import os
import sys

P2_DELAY = 23
CYNDAQUIL, PIDGEY = 0x9B, 0x10

# Both games, from power-on (each step: frames, P1's buttons, P2's buttons;
# P2's are then delayed by P2_DELAY frames).
B = "both"
TRADE_STEPS = [
    # the opening, the title, Continue, the saved game
    (600, "", ""), (2, B, "Start"), (200, "", ""), (2, B, "Start"), (150, "", ""),
    (2, B, "A"), (120, "", ""), (2, B, "A"), (300, "", ""),
    # Cherrygrove City's Pokemon Center: from the door mat (3,7), left onto the stairs (0,7)
    (52, B, "Left"), (150, "", ""),
    # upstairs: right to x=5, up to the Trade Center's attendant (5,2)
    (84, B, "Right"), (6, "", ""), (68, B, "Up"), (6, "", ""), (6, B, "Up"), (10, "", ""),
    # her questions: trade? save first? overwrite? then the link
    (2, B, "A"), (200, "", ""), (2, B, "A"), (80, "", ""), (2, B, "A"), (80, "", ""),
    (2, B, "A"), (80, "", ""), (2, B, "A"), (200, "", ""),
    (6, B, "A"), (100, "", ""), (6, B, "A"), (100, "", ""), (6, B, "A"), (100, "", ""),
    (6, B, "A"), (100, "", ""), (6, B, "A"), (200, "", ""), (6, B, "A"), (200, "", ""),
    (6, B, "A"), (200, "", ""), (6, B, "A"), (200, "", ""), (6, B, "A"), (400, "", ""),
]
# In the Trade Center: P1 to the left seat (3,4), P2 to the right one
# (6,4), each facing the console; A at it.
P1_SEAT = [(36, "Up"), (4, ""), (18, "Left"), (4, ""), (18, "Up"), (4, ""), (6, "Right"), (10, ""), (6, "A")]
P2_SEAT = [(18, "Right"), (4, ""), (36, "Up"), (4, ""), (18, "Right"), (4, ""), (18, "Up"), (4, ""),
           (6, "Left"), (10, ""), (6, "A")]
# The trade screen: P1 picks Cyndaquil (the first), P2 Pidgey (the
# second); TRADE; then yes to "Trade ... for ...?". P1's last A comes a
# second time: its question takes input later than the first A.
P1_TRADE = [(6, "A"), (66, ""), (6, "Right"), (30, ""), (6, "A"), (236, ""), (6, "A"), (60, ""), (6, "A"), (3000, "")]
P2_TRADE = [(6, ""), (6, "Down"), (60, ""), (6, "A"), (30, ""), (6, "Right"), (30, ""), (6, "A"), (200, ""), (6, "A")]
SEAT_TO_TRADE_WAIT = 900   # the "PLEASE WAIT!" while the games swap their parties


def expand(steps):
    out = []
    for n, b in steps:
        out += [b] * n
    return out


def save_inputs(out):
    here = os.path.dirname(os.path.abspath(__file__))
    lines = []
    for line in open(os.path.join(here, "pokemon-gold-opening.txt")):
        line = line.rstrip("\n")
        if not line or line.startswith("#"):
            continue
        n, held = line.split(" ", 1)
        lines += ["" if held == "-" else held] * int(n)
    open(out, "w").write("\n".join(lines) + "\n")


def trade_inputs(out):
    p1, p2 = [], []
    for n, who, b in TRADE_STEPS:
        p1 += [b if who == B else ""] * n
        p2 += [b if who == B else ""] * n
    # the seats: from the same frame for both
    start = len(p1)
    p1 += expand(P1_SEAT)
    p2 += expand(P2_SEAT)
    n = max(len(p1), len(p2)) + SEAT_TO_TRADE_WAIT
    p1 += [""] * (n - len(p1))
    p2 += [""] * (n - len(p2))
    p1 += expand(P1_TRADE)
    p2 += expand(P2_TRADE)
    p2 = [""] * P2_DELAY + p2
    n = max(len(p1), len(p2))
    p1 += [""] * (n - len(p1))
    p2 += [""] * (n - len(p2))
    lines = ["|".join(x for x in (("P1 " + a) if a else "", ("P2 " + b) if b else "") if x) for a, b in zip(p1, p2)]
    open(out, "w").write("\n".join(lines) + "\n")
    del start


def party(path):
    w = open(path, "rb").read()
    at = 0xDA22 - 0xD000 + 0x1000   # wPartyCount, in work memory bank 1
    return list(w[at + 1:at + 1 + w[at]])


def check(p1, p2):
    a, b = party(p1), party(p2)
    name = {CYNDAQUIL: "Cyndaquil", PIDGEY: "Pidgey"}
    say = lambda p: "+".join(name.get(s, "%02x" % s) for s in p)
    print("P1 has %s, P2 has %s" % (say(a), say(b)))
    sys.exit(0 if a == [PIDGEY, PIDGEY] and b == [CYNDAQUIL, CYNDAQUIL] else 1)


if __name__ == "__main__":
    cmd = sys.argv[1]
    if cmd == "save-inputs":
        save_inputs(sys.argv[2])
    elif cmd == "trade-inputs":
        trade_inputs(sys.argv[2])
    elif cmd == "check":
        check(sys.argv[2], sys.argv[3])
    else:
        sys.exit(__doc__)
