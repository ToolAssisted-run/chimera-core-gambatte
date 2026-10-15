#!/bin/bash
# The core's tests. Each line says what was compared. The script ends with a
# count of tests that passed, failed and were skipped, and with an error if
# any failed. A skipped test is not a pass.
#
#   waterbox/run-gate.sh
#
# CHIMERA_ROOT=<a Chimera checkout> adds the test that runs the package in
# Chimera's own headless runner (waterbox/build-package.sh -r <it> first),
# and the check of the default key bindings' names.
#
# Needs the native build (meson setup build/meson-native ... && ninja) and
# the sandboxed core (waterbox/setup-guest.sh && ninja -C build/meson-guest):
# docs/BUILDING.md. And Python 3.
#
# WHAT RUNS WITH NO FILES OF YOURS: everything but the boot ROM and game
# tests. tests/make-testprog.py writes a cartridge of this repository's own
# that reads the buttons, keeps a battery-backed memory and reads the
# cartridge's clock; gambatte-core's own hardware tests are assembled from
# their sources with upstream's own assembler.
#
# WHAT NEEDS YOUR FILES (skipped without them):
#   tests/roms-local/  the boot ROMs, under the names the core mounts them by:
#                      dmg_boot.bin, cgb_boot.bin, agb_boot.bin, sgb2_boot.bin
#   tests/games-local/ any .gb and .gbc games; each runs 2,000 frames
#
# The ways a test passes on a broken thing (Chimera's docs/gates.md) that
# these are written against: a machine that did nothing agrees with itself,
# so a run is first checked to have got somewhere; the end of a run hides a
# run that went another way and came back, so runs are compared as a stream
# of digests; and a comparison that cannot fail proves nothing, so each
# "the same" has a run beside it that must come out different.
set -u
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
native="$root/build/meson-native/run-native"
runwbx="$root/build/meson-native/run-wbx"
core="$root/build/meson-guest/core.wbx"
chimera_root="${CHIMERA_ROOT:-}"
roms="$root/tests/roms-local"
games="$root/tests/games-local"

work="$(mktemp -d)"
# GATE_KEEP=1 keeps the work folder (the runs' output and dumps) and says where
if [ -n "${GATE_KEEP:-}" ]; then trap 'echo "work folder kept: $work"' EXIT; else trap 'rm -rf "$work"' EXIT; fi
ok=0; failed=0; skipped=0
report() { # <test> <PASS|FAIL|SKIP> <what>
	printf '%-24s %-6s %s\n' "$1" "$2" "$3"
	case "$2" in PASS) ok=$((ok + 1)) ;; FAIL) failed=$((failed + 1)) ;; *) skipped=$((skipped + 1)) ;; esac
}
# check <test> <what> <condition...>
check() {
	ck_test="$1"; ck_what="$2"; shift 2
	if "$@"; then report "$ck_test" PASS "$ck_what"; else report "$ck_test" FAIL "$ck_what"; fi
}
stream() { grep '^frame' "$1"; }
same() { [ -s "$1" ] && [ -s "$2" ] && cmp -s "$1" "$2"; }
differ() { [ -s "$1" ] && [ -s "$2" ] && ! cmp -s "$1" "$2"; }
# field <file> <name>: the value after a name in the file's last report line
field() { stream "$1" | tail -1 | awk -v f="$2" '{for (i = 1; i <= NF; i++) if ($i == f) { print $(i + 1); exit }}'; }
pictures() { stream "$1" | awk '{print $7}' | sort -u | wc -l; }
sounds() { stream "$1" | awk '{print $10}' | sort -u | wc -l; }
size() { stream "$1" | tail -1 | awk '{print $6}'; }
# r <WRAM dump> <name>: what the test program left in its memory
r() { python3 "$root/tests/results.py" "$1" "$2" 2> /dev/null; }
# folder <name> <settings json> <game> [<save data>] [<firmware>...]: a work
# folder as the sandbox mounts a project - every file under its name
folder() {
	fd_name="$1"; fd_settings="$2"; fd_game="$3"; fd_save="${4:-}"
	shift 3; [ $# -gt 0 ] && shift
	rm -rf "${work:?}/$fd_name"; mkdir -p "$work/$fd_name"
	cp "$fd_game" "$work/$fd_name/$(basename "$fd_game")"
	if [ -n "$fd_save" ]; then
		cp "$fd_save" "$work/$fd_name/$(basename "$fd_save")"
		printf '{"game":["%s"],"savedata":["%s"]}' "$(basename "$fd_game")" "$(basename "$fd_save")" > "$work/$fd_name/slots"
	else
		printf '{"game":["%s"]}' "$(basename "$fd_game")" > "$work/$fd_name/slots"
	fi
	for fw in "$@"; do cp "$fw" "$work/$fd_name/"; done
	printf '%s' "$fd_settings" > "$work/$fd_name/settings"
}
nat() { nt_out="$1"; nt_dir="$2"; shift 2; "$native" "$work/$nt_dir" "$@" > "$work/$nt_out.out" 2> "$work/$nt_out.err"; }
box() { bx_out="$1"; bx_dir="$2"; shift 2; "$runwbx" "$core" "$work/$bx_dir" "$@" > "$work/$bx_out.out" 2> "$work/$bx_out.err"; }
# index <folder> <control>: the control's place in the panel
index() { "$native" "$work/$1" --frames 1 --list-panel 2> /dev/null | grep "'$2'" | head -1 | awk '{print $2}'; }
# repeats <file> <first> <second> <count> <step>: do the report lines from
# frame <second> on repeat those from frame <first> on (run-wbx --rewind)
repeats() {
	awk -v a="$2" -v b="$3" -v n="$4" -v step="$5" '
		/^frame/ { f = $2; $1 = ""; $2 = ""; sub(/ lag [0-9]+$/, ""); line[f] = $0 }
		END {
			for (i = step; i < n; i += step) {
				if (!((a + i) in line) || !((b + i) in line) || line[a + i] != line[b + i]) exit 1
				seen++
			}
			exit seen > 0 ? 0 : 1
		}' "$1"
}

if [ ! -x "$native" ] || [ ! -x "$runwbx" ] || [ ! -f "$core" ]; then
	echo "not built: this needs build/meson-native/run-native and run-wbx, and build/meson-guest/core.wbx (docs/BUILDING.md)" >&2
	exit 1
fi

# ------------------------------------------------- the source and the package
patches="$(ls "$root"/patches/*.patch 2> /dev/null | wc -l)"
check "source:pinned" "extern/gambatte-core is the pinned commit, with the $patches patches of patches/" \
	sh -c "sh '$here/apply-patches.sh' > '$work/patches.log' 2>&1 && [ \"\$(git -C '$root' ls-files -s extern/gambatte-core | awk '{print \$2}')\" = \"\$(git -C '$root/extern/gambatte-core' rev-parse HEAD)\" ]"

stale=""
for src in "$here"/cinterface.cpp "$root"/meson.build "$root"/patches/*.patch; do
	[ -e "$src" ] || continue
	[ "$core" -nt "$src" ] || stale="$stale $(basename "$src")"
	[ "$native" -nt "$src" ] || stale="$stale $(basename "$src")(native)"
done
check "package:fresh" "core.wbx and run-native are not older than their sources${stale:+ (older than:$stale)}" [ -z "$stale" ]

# ------------------------------------------------- the test program
progs="$work/progs"
mkdir -p "$progs"
python3 "$root/tests/make-testprog.py" "$progs/testprog.gb"
python3 "$root/tests/make-testprog.py" "$progs/testprog.gbc"
python3 "$root/tests/make-testprog.py" "$progs/nopad.gb" nopad
off='{"bootRom":"off"}'
folder t "$off" "$progs/testprog.gb"
folder c "$off" "$progs/testprog.gbc"
folder q "$off" "$progs/nopad.gb"

"$native" "$work/t" --frames 1 --list-panel > "$work/panel.out" 2> /dev/null
check "package:controls" "each of the package's machines declares the core's own $(grep -c '^panel' "$work/panel.out") buttons, in the core's order; on a Game Boy the first pad and Power are live, the Super Game Boy's three other pads are not" \
	python3 - "$here/waterbox.config" "$work/panel.out" <<'PY'
import json, sys
cfg = json.load(open(sys.argv[1]))
lines = [l for l in open(sys.argv[2]).read().splitlines() if l.startswith("panel ")]
buttons = [l[l.index("'") + 1:l.rindex("'")] for l in lines]
live = [l[l.index("'") + 1:l.rindex("'")] for l in lines if l.endswith("active")]
want_live = ["P1 " + b for b in ("Up", "Down", "Left", "Right", "Start", "Select", "B", "A")] + ["Power"]
sys.exit(0 if all(m["input"]["buttons"] == buttons for m in cfg["machines"]) and len(buttons) == 33 and live == want_live else 1)
PY

if [ -n "$chimera_root" ] && [ -f "$chimera_root/source/gui/Chimera.Input/KeyMouseInput/DistinctKey.cs" ]; then
	check "package:key-names" "every host key the package's default bindings name is a key Chimera knows: $(python3 "$root/tests/check-key-names.py" "$chimera_root" "$here/default_keybinds.json" 2>&1)" \
		sh -c "python3 '$root/tests/check-key-names.py' '$chimera_root' '$here/default_keybinds.json' > /dev/null 2>&1"
else
	report "package:key-names" SKIP "needs CHIMERA_ROOT, whose source names the keys Chimera knows"
fi

nat idle t --frames 300 --report 25 --dump-domain WRAM "$work/idle.wram"
check "prog:runs" "the test program runs with no boot ROM: it counted $(r "$work/idle.wram" frames) frames in 300, with $(pictures "$work/idle.out") different pictures in 12 reports, at $(size "$work/idle.out")" \
	sh -c "[ '$(r "$work/idle.wram" frames)' -ge 295 ] && [ $(pictures "$work/idle.out") -ge 12 ] && [ '$(size "$work/idle.out")' = 160x144 ]"

# each button held alone from frame 60; what the program then read
: > "$work/buttons.txt"
for ((b = 0; b < 8; b++)); do
	"$native" "$work/t" --frames 100 --press "$b":60:60 --dump-domain WRAM "$work/eb.wram" > /dev/null 2>&1
	echo "$b $(r "$work/eb.wram" padSeen)" >> "$work/buttons.txt"
done
# the program's bits: Down, Up, Left, Right on top, Start, Select, B, A below
check "prog:buttons" "each of P1's 8 buttons, held alone, is the Game Boy's own bit for it and no other ($(awk '{printf "%s ", $2}' "$work/buttons.txt"))" \
	[ "$(awk '{printf "%s ", $2}' "$work/buttons.txt")" = "64 128 32 16 8 4 2 1 " ]

nat lag q --frames 300 --report 100
check "prog:lag" "a frame is a lag frame when the program did not read the buttons: the program that reads them every frame has $(field "$work/idle.out" lag) in 300, its twin that never does $(field "$work/lag.out" lag) (all but the first, which the boot ROM's state reads)" \
	[ "$(field "$work/idle.out" lag) $(field "$work/lag.out" lag)" = "0 299" ]

nat dark t --frames 200 --report 10 --press 5:50:60 --dump-domain WRAM "$work/dark.wram"
nat dark80 t --frames 80 --report 80 --press 5:50:60 --vid-out "$work/dark80.vid"
dark_pics="$(stream "$work/dark.out" | awk '$2 >= 60 && $2 <= 110 {print $7}' | sort -u | wc -l)"
dark_white="$(python3 -c "import sys; d = open(sys.argv[1], 'rb').read().split(b'\n', 1)[1]; print(int(len(d) == 160 * 144 * 4 and set(d) == {255}))" "$work/dark80.vid" 2> /dev/null)"
check "prog:screen-off" "with the screen off (Select held, frames 50 to 110) the console shows a blank white screen, every frame the same ($dark_pics picture in the 6 reports; white: $dark_white), and the program goes on counting ($(r "$work/dark.wram" frames) frames in 200)" \
	sh -c "[ $dark_pics = 1 ] && [ '$dark_white' = 1 ] && [ $(pictures "$work/dark.out") -ge 8 ] && [ '$(r "$work/dark.wram" frames)' -ge 195 ]"

nat clock t --frames 1200 --report 600 --dump-domain WRAM "$work/clock.wram"
nat clock600 t --frames 600 --report 600 --dump-domain WRAM "$work/clock600.wram"
check "prog:clock" "the cartridge's clock counts the machine's own time: $(r "$work/clock600.wram" clockSeconds) seconds after 600 frames and $(r "$work/clock.wram" clockSeconds) after 1,200 (70,224 cycles of 4,194,304 a frame)" \
	[ "$(r "$work/clock600.wram" clockSeconds) $(r "$work/clock.wram" clockSeconds)" = "10 20" ]

mkdir -p "$work/snd-av" "$work/idle-av"
nat snd t --frames 600 --report 5 --press 7:100:200 --av-stream "$work/snd-av"
nat idleav t --frames 600 --report 5 --av-stream "$work/idle-av"
total="$(( $(wc -c < "$work/snd-av/audio.raw") / 4 ))"
want="$(( 600 * 35112 / 64 ))"
check "prog:sound" "$total sound samples in 600 frames, one in 64 of the machine's 2,097,152 a second (600 frames of 35,112 would be $want; the first frame is cut short where the machine starts); the pitch follows the buttons: with A held the sound differs from the run without" \
	sh -c "[ $((want - total)) -ge 0 ] && [ $((want - total)) -le 549 ] && ! cmp -s '$work/snd-av/audio.raw' '$work/idle-av/audio.raw' && [ $(sounds "$work/snd.out") -ge 3 ]"

pw="$(index t Power)"
nat power t --frames 300 --press "$pw":100:1 --dump-domain WRAM "$work/power.wram"
check "prog:power" "Power starts the console again: the program counted $(r "$work/power.wram" frames) frames after it (of 200), and its count of starts in the cartridge's memory is $(r "$work/power.wram" starts) (1 after one start more)" \
	sh -c "[ '$(r "$work/power.wram" frames)' -ge 195 ] && [ '$(r "$work/power.wram" frames)' -le 200 ] && [ '$(r "$work/power.wram" starts)' = 1 ]"

# the saved data: exported after a run, and a project begins from it
mkdir -p "$work/sv"
nat sv t --frames 300 --press 7:100:5 --savedata-out "$work/sv"
sav="$work/sv/testprog.sav"
folder sv2 "$off" "$progs/testprog.gb" "$sav"
nat sv2 sv2 --frames 300 --dump-domain WRAM "$work/sv2.wram"
check "prog:save-data" "Export Save Data writes testprog.sav: the 8 KB of memory and the clock's 22 bytes ($(wc -c < "$sav" 2> /dev/null) bytes); a project with it begins from it: the count of starts goes on ($(r "$work/sv2.wram" starts)) and the clock from its 5 seconds ($(r "$work/sv2.wram" clockSeconds) after 300 frames more)" \
	sh -c "[ '$(wc -c < "$sav" 2> /dev/null)' = 8214 ] && [ '$(r "$work/sv2.wram" starts)' = 1 ] && [ '$(r "$work/sv2.wram" clockSeconds)' = 10 ]"

nat bus t --frames 300 --report 25 --dump-domain bus:49152:8 "$work/bus.bin"
head -c 8 "$work/idle.wram" > "$work/idle8.bin"
stream "$work/bus.out" > "$work/bus.stream"; stream "$work/idle.out" > "$work/idle.stream"
check "prog:bus" "the system bus at C000 holds what the work memory does ($(od -An -tx1 -N6 "$work/bus.bin" | tr -d ' ')), and reading it changes nothing in the run" \
	sh -c "cmp -s '$work/bus.bin' '$work/idle8.bin' && cmp -s '$work/bus.stream' '$work/idle.stream'"

# ------------------------------------------------- the consoles
nat gbc c --frames 300 --report 25 --dump-domain WRAM "$work/gbc.wram"
folder gbca '{"bootRom":"off","model":"GBA"}' "$progs/testprog.gbc"
folder sgb '{"bootRom":"off","model":"SGB2"}' "$progs/testprog.gb"
folder gbcdmg '{"bootRom":"off","model":"GBC"}' "$progs/testprog.gb"
nat gba gbca --frames 300 --report 25 --dump-domain WRAM "$work/gba.wram"
nat sgb sgb --frames 300 --report 25 --dump-domain WRAM "$work/sgb.wram" --vid-out "$work/sgb.vid"
nat gbcdmg gbcdmg --frames 300 --report 25 --dump-domain WRAM "$work/gbcdmg.wram"
stream "$work/gbc.out" | awk '{print $7}' > "$work/gbc.pics"; stream "$work/idle.out" | awk '{print $7}' > "$work/idle.pics"
check "consoles:no-model" "with no Model given (Chimera always gives one; the test runners need not) a .gbc file runs on the Game Boy Color (its own colours: another picture than the .gb file's) and counts its frames ($(r "$work/gbc.wram" frames))" \
	sh -c "[ '$(r "$work/gbc.wram" frames)' -ge 295 ] && ! cmp -s '$work/gbc.pics' '$work/idle.pics'"
check "consoles:models" "GBA, SGB2 (whose SNES starts the Game Boy about 120 frames late), and GBC with a Game Boy game each run the program (frames $(r "$work/gba.wram" frames), $(r "$work/sgb.wram" frames), $(r "$work/gbcdmg.wram" frames)); the Game Boy, the Game Boy Color, the Game Boy Color with a Game Boy game and the Super Game Boy each draw a picture of their own (the Game Boy Advance draws the Game Boy Color's: the core applies no colour correction); the Super Game Boy's is $(size "$work/sgb.out") with its border" \
	sh -c "[ '$(r "$work/gba.wram" frames)' -ge 295 ] && [ '$(r "$work/sgb.wram" frames)' -ge 150 ] && [ '$(r "$work/gbcdmg.wram" frames)' -ge 295 ] && [ '$(size "$work/sgb.out")' = 256x224 ] && [ $(for f in idle gbc sgb gbcdmg; do stream "$work/$f.out" | tail -1 | awk '{print $7}'; done | sort -u | wc -l) = 4 ]"
check "consoles:sgb-screen" "the Super Game Boy's picture has the Game Boy's screen at its place, 48 pixels in and 40 down: $(python3 "$root/tests/sgb-screen.py" "$work/sgb.vid" 2>&1)" \
	python3 "$root/tests/sgb-screen.py" "$work/sgb.vid"
"$native" "$work/sgb" --frames 1 --list-panel > "$work/sgb-panel.out" 2> /dev/null
check "consoles:sgb-pads" "on the Super Game Boy all four pads are live ($(grep -c 'active$' "$work/sgb-panel.out") controls)" \
	[ "$(grep -c 'active$' "$work/sgb-panel.out")" = 33 ]

# ------------------------------------------------- native and sandbox, states
# Select held from frame 280 to 320 turns the program's screen off around
# the frame where --session moves the machine to a new sandbox (300)
allrun="--frames 600 --report 10 --press 7:50:40 --press 0:80:30 --press 4:200:5 --press 5:280:40 --press 2:330:60 --press $pw:400:1"
nat all t $allrun
nat idle10 t --frames 600 --report 10
box all-box t $allrun
box all-rerecord t $allrun --rerecord
box all-session t $allrun --session
for n in all all-box all-rerecord all-session idle10; do stream "$work/$n.out" > "$work/$n.stream"; done
check "prog:sandbox" "native and sandbox print the same $(wc -l < "$work/all.stream") digests of memory, picture and sound, with buttons pressed and Power" \
	same "$work/all.stream" "$work/all-box.stream"
check "prog:presses-change-the-run" "the run with presses differs from the idle one" differ "$work/all.stream" "$work/idle10.stream"
check "prog:state-every-frame" "a state saved and loaded every frame changes nothing ($(cat "$work/all-rerecord.out" "$work/all-rerecord.err" | grep -o 'stateBytes=[0-9]*' | head -1 | sed 's/stateBytes=//') bytes a state)" same "$work/all-box.stream" "$work/all-rerecord.stream"
check "prog:state-other-box" "a state moved to a new sandbox half way changes nothing" same "$work/all-box.stream" "$work/all-session.stream"
box rewind t --frames 1600 --report 5 --rewind --press 5:790:40 --press 5:1190:40
check "prog:state-rewind" "a state kept at frame 800, with the screen off, and loaded at 1200: the frames after the load repeat those after the save" \
	repeats "$work/rewind.out" 800 1200 380 5
for m in gbc sgb; do
	d="$m"; [ "$m" = gbc ] && d=c
	nat "s-$m" "$d" $allrun
	box "s-$m-box" "$d" $allrun
	box "s-$m-rr" "$d" $allrun --rerecord
	for n in "s-$m" "s-$m-box" "s-$m-rr"; do stream "$work/$n.out" > "$work/$n.stream"; done
done
check "consoles:sandbox" "on the Game Boy Color and the Super Game Boy too, native, sandbox and a state saved and loaded every frame print the same digests ($(wc -l < "$work/s-gbc.stream") and $(wc -l < "$work/s-sgb.stream"))" \
	sh -c "cmp -s '$work/s-gbc.stream' '$work/s-gbc-box.stream' && cmp -s '$work/s-gbc.stream' '$work/s-gbc-rr.stream' && cmp -s '$work/s-sgb.stream' '$work/s-sgb-box.stream' && cmp -s '$work/s-sgb.stream' '$work/s-sgb-rr.stream' && [ -s '$work/s-sgb.stream' ]"

# ------------------------------------------------- what is refused
mkdir -p "$work/none"
printf '{"game":[]}' > "$work/none/slots"; printf '{}' > "$work/none/settings"
nat none none --frames 1
check "refused:no-game" "a machine with no game is refused, and says so" grep -q "Init failed.*no game was given" "$work/none.out" "$work/none.err"
printf 'not a game' > "$work/junk.gb"
folder junk "$off" "$work/junk.gb"
nat junk junk --frames 1
check "refused:not-a-game" "a file too short to be a game is refused, and says so" grep -q "Init failed.*too short to be a Game Boy game" "$work/junk.out" "$work/junk.err"
folder nobios '{}' "$progs/testprog.gb"
nat nobios nobios --frames 1
check "refused:no-boot-rom" "with Boot ROM 'on' and no boot ROM given, the machine is refused, and says which file it needs" grep -q "Init failed.*dmg_boot.bin is missing" "$work/nobios.out" "$work/nobios.err"

# ------------------------------------------------- upstream's own tests
python3 "$root/tests/hwtests.py" "$native" "$work/hw" > "$work/hw.out" 2> "$work/hw.err"
grep '^FAILED' "$work/hw.out" | cut -d' ' -f2- | sort > "$work/hw.failed"
grep -v '^#' "$root/tests/hwtests-expected-failures.txt" | sort > "$work/hw.expected"
check "upstream:hwtests" "gambatte-core's own hardware tests through the core, judged as upstream's runner judges them: $(tail -1 "$work/hw.out"); it fails exactly the $(wc -l < "$work/hw.expected") Game Boy Advance tests upstream's own runner fails" \
	sh -c "[ -s '$work/hw.out' ] && cmp -s '$work/hw.failed' '$work/hw.expected' && tail -1 '$work/hw.out' | grep -q '^GB [0-9]'"
python3 "$root/tests/hwtests.py" "$native" "$work/hw" 25 > "$work/hw-n25.out" 2> /dev/null
python3 "$root/tests/hwtests.py" "$runwbx $core" "$work/hw" 25 > "$work/hw-b25.out" 2> /dev/null
check "upstream:hwtests-sandbox" "one test in 25 through the sandbox gives the native build's results ($(tail -1 "$work/hw-b25.out"))" \
	same "$work/hw-n25.out" "$work/hw-b25.out"

# ------------------------------------------------- with the boot ROMs
# the frame the program starts at with no boot ROM, from the runs above
nobios_GB=$((300 - $(r "$work/idle.wram" frames))); nobios_GBC=$((300 - $(r "$work/gbc.wram" frames)))
nobios_GBA=$((300 - $(r "$work/gba.wram" frames))); nobios_SGB2=$((300 - $(r "$work/sgb.wram" frames)))
for pair in "dmg_boot.bin:GB:testprog.gb" "cgb_boot.bin:GBC:testprog.gbc" "agb_boot.bin:GBA:testprog.gbc" "sgb2_boot.bin:SGB2:testprog.gb"; do
	fw="${pair%%:*}"; rest="${pair#*:}"; model="${rest%%:*}"; prog="${rest#*:}"
	eval "nobios=\$nobios_$model"
	if [ ! -f "$roms/$fw" ]; then
		report "boot:$model" SKIP "no $fw in tests/roms-local"
		continue
	fi
	folder "b-$model" "{\"model\":\"$model\"}" "$progs/$prog" "" "$roms/$fw"
	nat "b-$model" "b-$model" --frames 900 --report 50 --dump-domain WRAM "$work/b-$model.wram"
	box "b-$model-box" "b-$model" --frames 900 --report 50
	stream "$work/b-$model.out" > "$work/b-$model.stream"; stream "$work/b-$model-box.out" > "$work/b-$model-box.stream"
	started=$((900 - $(r "$work/b-$model.wram" frames)))
	check "boot:$model" "the $model boot ROM runs first, then the program, which started at frame $started (at frame $nobios with no boot ROM); the sandbox gives the native build's digests" \
		sh -c "[ $started -ge $((nobios + 20)) ] && [ $started -lt 800 ] && cmp -s '$work/b-$model.stream' '$work/b-$model-box.stream'"
done

# ------------------------------------------------- the games of yours
found=0
for g in "$games"/*.gb "$games"/*.gbc; do
	[ -f "$g" ] || continue
	found=1
	n="$(basename "$g")"
	ext="${n##*.}"
	fw="dmg_boot.bin"; [ "$ext" = gbc ] && fw="cgb_boot.bin"
	if [ -f "$roms/$fw" ]; then st='{}'; with="with the boot ROM"; else st="$off"; fw=""; with="with no boot ROM"; fi
	folder "g-$n" "$st" "$g" "" ${fw:+"$roms/$fw"}
	gp="--frames 2000 --report 100 --press 4:600:5 --press 4:900:5 --press 7:1100:5 --press 4:1300:5 --press 7:1500:5 --press 3:1600:200"
	nat "g-$n" "g-$n" $gp
	box "g-$n-box" "g-$n" $gp
	stream "$work/g-$n.out" > "$work/g-$n.stream"; stream "$work/g-$n-box.out" > "$work/g-$n-box.stream"
	check "game:$n" "runs 2,000 frames $with, Start and A pressed: $(pictures "$work/g-$n.out") different pictures and $(sounds "$work/g-$n.out") sounds in 20 reports; the sandbox gives the native build's digests" \
		sh -c "[ \$(wc -l < '$work/g-$n.stream') = 20 ] && [ $(pictures "$work/g-$n.out") -ge 5 ] && cmp -s '$work/g-$n.stream' '$work/g-$n-box.stream'"
done
[ "$found" = 1 ] || report "game:*" SKIP "no .gb or .gbc game in tests/games-local"

# ------------------------------------------------- Chimera's engine
runner="$chimera_root/build/dll/chimera-run"
package="$chimera_root/build/Cores/gambatte.chimeraCore"
if [ -n "$chimera_root" ] && [ -x "$runner" ] && [ -f "$package" ]; then
	mkdir -p "$work/proj"
	cp "$progs/testprog.gb" "$work/proj/"
	python3 "$root/tests/make-project.py" "$package" "$work/proj/test.chimeraProject" 300 bootRom=off \
		"press=P1 A:199:101" "press=P1 Down:149:151" panel="$work/panel.out" -- game="$work/proj/testprog.gb" > /dev/null
	"$runner" --project "$work/proj/test.chimeraProject" "$package" --files "$work/proj" --frames 300 \
		--dump "WRAM=$work/engine.wram" > "$work/engine.log" 2>&1
	box eng t --frames 300 --press "$(index t "P1 A")":200:101 --press "$(index t "P1 Down")":150:151 --dump-domain WRAM "$work/eng.wram"
	check "engine:project" "a project of the test program with presses plays in Chimera's headless runner to the sandbox runner's memory ($(wc -c < "$work/eng.wram" 2> /dev/null) bytes; the program read the buttons as $(r "$work/engine.wram" padSeen), Down and A)" \
		sh -c "cmp -s '$work/engine.wram' '$work/eng.wram' && [ '$(r "$work/engine.wram" padSeen)' = 129 ]"
else
	report "engine:project" SKIP "needs CHIMERA_ROOT with build/dll/chimera-run and build/Cores/gambatte.chimeraCore"
fi

echo
echo "$ok passed, $failed failed, $skipped skipped"
[ "$failed" -eq 0 ]
