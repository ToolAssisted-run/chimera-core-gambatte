#!/usr/bin/env python3
"""Runs gambatte-core's own hardware tests (test/hwtests in the submodule)
through the core, and judges them as upstream's test runner
(test/testrunner.cpp) does.

usage: hwtests.py <run-native or "run-wbx core.wbx"> <work folder> [<every Nth test>]

Each test is a small program, assembled from its .asm source by upstream's
own assembler (test/qdgbas.py), that ends by showing hexadecimal digits on
the screen or by making sound or none. Its name says what a real console
shows: "_dmg08_out" for a Game Boy, "_cgb04c_out" for a Game Boy Color,
"_out" alone for both, followed by the digits, or by "audio0" (silence) or
"audio1" (sound). As upstream's runner does, each test runs on every
console its name gives a result for, a Game Boy Color result also stands for
the Game Boy Advance, and the tests start with no boot ROM.

The tests a picture file judges (upstream's .png files) are left out: they
compare whole colours, and upstream's runner sets palettes of its own for
them that a core does not have.

It prints one line for each test that did not show what its name says, then
a count for each console. Upstream's own runner fails 34 of the Game Boy
Advance tests, which upstream marks as having no real results for that
console yet ("FIXME: Actual AGB results"); the same 34 fail here, and
tests/hwtests-expected-failures.txt lists them.
"""
import os
import shutil
import subprocess
import sys

# upstream's runner's digits, 8x8 each, read from its source
DIGITS = {}


def _digits():
    # the 16 tiles of upstream's runner (testrunner.cpp, tileFromChar),
    # O = 0x000000, _ = 0xF8F8F8
    src = open(os.path.join(UPSTREAM, "test", "testrunner.cpp")).read()
    at = src.index("#define O 0x000000")
    body = src[at + len("#define O 0x000000"):src.index("#undef O", at)]
    cells = [c for c in body.replace("\t", " ").split(",") if c.strip() in ("O", "_")]
    cells = [c.strip() for c in cells]
    assert len(cells) == 16 * 64, len(cells)
    for n in range(16):
        DIGITS["0123456789ABCDEF"[n]] = cells[n * 64:(n + 1) * 64]


UPSTREAM = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "extern", "gambatte-core")


def judge(name, outkey, picture, width, audio):
    """True when the run shows what the name after outkey says."""
    s = os.path.splitext(name)[0]
    rest = s[s.index(outkey) + len(outkey):]
    if rest.startswith("audio0"):
        return len(set(audio)) <= 1
    if rest.startswith("audio1"):
        return len(set(audio)) > 1
    for i, ch in enumerate(rest):
        tile = DIGITS.get(ch.upper())
        if tile is None:
            break
        for y in range(8):
            for x in range(8):
                px = picture[y * width + i * 8 + x] & 0xF8F8F8
                want = 0 if tile[y * 8 + x] == "O" else 0xF8F8F8
                if px != want:
                    return False
    return True


def modes(name):
    """(model, outkey) pairs, as upstream's runner chooses them."""
    s = os.path.splitext(name)[0]
    dmg = cgb = None
    if "dmg08_cgb04c_out" in s:
        dmg = cgb = "dmg08_cgb04c_out"
    elif "dmg08_out" in s:
        dmg = "dmg08_out"
        if "cgb04c_out" in s:
            cgb = "cgb04c_out"
    elif "_out" in s:
        cgb = "_out"
    out = []
    if cgb:
        out += [("GBA", cgb), ("GBC", cgb)]
    if dmg:
        out.append(("GB", dmg))
    return out


def run(runner, folder, rom, model):
    shutil.rmtree(folder, ignore_errors=True)
    os.makedirs(folder)
    shutil.copy(rom, os.path.join(folder, "test.gb"))
    open(os.path.join(folder, "slots"), "w").write('{"game":["test.gb"]}')
    open(os.path.join(folder, "settings"), "w").write('{"model":"%s","bootRom":"off"}' % model)
    av = os.path.join(folder, "av")
    os.mkdir(av)
    vid = os.path.join(folder, "last.vid")
    # upstream runs 15 frames' worth of samples with no boot ROM
    subprocess.run(runner + [folder, "--frames", "15", "--report", "15", "--vid-out", vid, "--av-stream", av],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
    data = open(vid, "rb").read()
    head, pixels = data.split(b"\n", 1)
    w, h = (int(v) for v in head.split())
    picture = [int.from_bytes(pixels[i * 4:i * 4 + 4], "little") & 0xFFFFFF for i in range(w * h)]
    audio_raw = open(os.path.join(av, "audio.raw"), "rb").read()
    # the last frame's sound: the frames' sample counts are not stored, so
    # the last 549 pairs (one frame's worth at 32,768 a second)
    last = audio_raw[-549 * 4:]
    audio = [last[i:i + 4] for i in range(0, len(last), 4)]
    return picture, w, audio


def main():
    runner = sys.argv[1].split()
    work = sys.argv[2]
    step = int(sys.argv[3]) if len(sys.argv) > 3 else 1
    _digits()
    src = os.path.join(work, "hwtests")
    if not os.path.isdir(src):
        shutil.copytree(os.path.join(UPSTREAM, "test", "hwtests"), src)
        shutil.copy(os.path.join(UPSTREAM, "test", "qdgbas.py"), work)
        asms = sorted(os.path.join(dp, f) for dp, _, fs in os.walk(src) for f in fs if f.endswith(".asm"))
        for i in range(0, len(asms), 200):
            subprocess.run([sys.executable, "-W", "ignore", os.path.join(work, "qdgbas.py")] + asms[i:i + 200],
                           cwd=work, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
    roms = sorted(os.path.join(dp, f) for dp, _, fs in os.walk(src) for f in fs
                  if f.endswith(".gb") or f.endswith(".gbc"))
    roms = roms[::step]
    folder = os.path.join(work, "run")
    os.makedirs(folder, exist_ok=True)
    counts = {m: [0, 0] for m in ("GB", "GBC", "GBA")}
    failed = []
    for rom in roms:
        name = os.path.basename(rom)
        for model, key in modes(name):
            picture, w, audio = run(runner, folder, rom, model)
            ok = judge(name, key, picture, w, audio)
            counts[model][0] += 1
            counts[model][1] += ok
            if not ok:
                failed.append("%s %s" % (model, os.path.relpath(rom, work)))
    for f in failed:
        print("FAILED", f)
    print(" ".join("%s %d/%d" % (m, c[1], c[0]) for m, c in counts.items()))


if __name__ == "__main__":
    main()
