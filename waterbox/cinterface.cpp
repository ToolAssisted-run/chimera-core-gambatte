// cinterface.cpp - Gambatte as a Chimera core: the functions Chimera calls.
//
// The machine is libgambatte from Gambatte-Speedrun (gambatte-core), the
// Game Boy and Game Boy Color emulator speedrunners use. Its own programs
// around it (a Qt window, a test runner) are not built; this file takes
// their place inside the sandbox. It loads the game, the boot ROM and the
// saved data, gives the machine the controls, runs it to the end of a
// picture, and turns its sound into a rate Chimera can play.
//
// The Game Boy Link machines are two, three or four Game Boys joined by
// link cables, each with its own game: libgambatte machines run side by
// side, a few samples at a time, with the byte each sends through its link
// port passed to the one it is joined to.
//
// SPDX-License-Identifier: MIT
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <strings.h>

#include <emulibc.h>
#include <waterbox_settings.h>
#include <waterbox_slots.h>

#include "gambatte.h"

// ---- the computer's clock ----------------------------------------------------
// libgambatte asks the computer for the time in three places: a cartridge
// clock (MBC3, HuC3) in real-time mode, and the time it writes beside a
// cartridge clock's registers in the saved data, to move the clock on by
// the time that passed until the data is loaded again. The core runs the
// clock on the machine's own cycles (setTimeMode(true)), so only the second
// is left, and the answer has to be the same on every run: no time passes
// between saving and loading. musl's time() is the C library's, not the
// machine's, and this one is found first.
extern "C" time_t time(time_t *t)
{
	if (t)
		*t = 0;
	return 0;
}

// ---- what went wrong, for Chimera to show ----------------------------------
/* A text a person reads is a key of languages/en.json, each of its
 * arguments after a TEXT_ARG (chimera docs/translations.md). TEXT_ARG is a
 * string of its own so the byte after it is never read as more of its hex. */
#define TEXT_ARG "\x1f"

static char g_loadError[512];

// The models a game can run on, as the Model setting names them; the link
// machines' Game Boys are each one of the first three.
enum Model { kAuto, kGb, kGbc, kGba, kSgb2 };

// A frame is 70,224 cycles of the 4,194,304 Hz clock (59.73 a second); the
// machine's sound comes as one stereo sample every two cycles, 35,112 a
// frame. runFor stops at the end of each picture, and may run up to 2,064
// samples past what it was asked for.
enum : unsigned
{
	kSamplesPerFrame = 35112,
	kSampleOverrun = 2064,
	// samples a linked Game Boy may have run past the end of a frame, which
	// the next frame starts with, and the most a frame then holds
	kMaxIn = kSamplesPerFrame + 2 * kSampleOverrun,
	// the link machines run their Game Boys this many samples at a time (8
	// cycles), and look at the link ports in between, as BizHawk does
	kLinkStep = 4,
};

// ---- the Game Boys -------------------------------------------------------------
enum { kMaxGb = 4, kGbWidth = 160, kGbHeight = 144, kSgbWidth = 256, kSgbHeight = 224 };

struct Player
{
	Model model;
	unsigned loadFlags;
	char gameName[256];
	uint32_t draw[kGbWidth * kGbHeight];   // the picture as it is drawn
	uint32_t audio[kMaxIn];                // this frame's samples
	int carried;                           // samples run past the last frame's end
	unsigned padBits;
	bool powerWasHeld;
};
static gambatte::GB g_gb[kMaxGb];
static Player g_p[kMaxGb];
static int g_count;            // Game Boys in the machine: 1, or 2 to 4 linked
static bool g_link;            // a Game Boy Link machine
static bool g_sgb;
static uint64_t g_frameCount;

// the link cables: connected or pulled out, and for three or four Game
// Boys which of them are joined (see link_ports)
static bool g_connected = true;
static bool g_shifted, g_spaced;
static bool g_connWasHeld, g_shiftWasHeld, g_spaceWasHeld;

// ---- the picture -------------------------------------------------------------
// The Game Boy's screen is 160 by 144; the Super Game Boy draws it inside a
// 256 by 224 border; the link machines put their screens side by side. A
// Game Boy draws into its own buffer as it goes, and a finished picture is
// copied into g_picture, which is what Chimera reads.
enum { kPictureMax = kMaxGb * kGbWidth * kSgbHeight };
static uint32_t g_picture[kPictureMax];
static bool g_render = true;

static int picture_width(void) { return g_sgb ? kSgbWidth : g_count * kGbWidth; }
static int picture_height(void) { return g_sgb ? kSgbHeight : kGbHeight; }

static void show(int i)
{
	if (!g_render)
		return;
	if (g_sgb)
	{
		g_gb[0].updateScreenBorder(g_picture, kSgbWidth);
		return;
	}
	const int pitch = g_count * kGbWidth;
	for (int y = 0; y < kGbHeight; y++)
		memcpy(g_picture + y * pitch + i * kGbWidth, g_p[i].draw + y * kGbWidth, kGbWidth * sizeof(uint32_t));
}

// ---- the sound -------------------------------------------------------------
// A Game Boy's own sound is 2,097,152 samples a second. The Super Game
// Boy's SNES sound chip adds one every 65 of those, and a HuC3 cartridge's
// speaker one for each. All are added on the machine's timeline, as BizHawk
// does (the Game Boy's at half its level; the linked Game Boys' together,
// each at its share), and brought down to 32,768 samples a second - one in
// 64 - through a third-order CIC filter: three running sums, one sample in
// 64 kept, three differences. It is integer arithmetic, so every computer
// gives the same samples, and it removes what would otherwise fold back
// into the audible range.
enum { kDecimation = 64, kCicShift = 18 }; // 64^3 = 2^18
enum { kMaxOut = kMaxIn / kDecimation + 2 };
static short g_sgbAudio[2048 * 2];
static short g_mbcAudio[kMaxIn * 2];
static int32_t g_mixL[kMaxIn], g_mixR[kMaxIn];
static int16_t g_audio[kMaxOut * 2];
static int g_audioFrames;
static int32_t g_sgbHeld[2], g_mbcHeld[kMaxGb][2];

struct Cic
{
	uint64_t i1, i2, i3, d1, d2, d3;
};
static Cic g_cic[2];
static unsigned g_cicPhase;

static int16_t cic_out(Cic &c)
{
	const uint64_t y0 = c.i3;
	const uint64_t y1 = y0 - c.d1;
	c.d1 = y0;
	const uint64_t y2 = y1 - c.d2;
	c.d2 = y1;
	const uint64_t y3 = y2 - c.d3;
	c.d3 = y2;
	int64_t v = (int64_t)y3 >> kCicShift;
	return (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
}

static void decimate(int n)
{
	for (int i = 0; i < n; i++)
	{
		const int32_t x[2] = { g_mixL[i], g_mixR[i] };
		for (int ch = 0; ch < 2; ch++)
		{
			Cic &c = g_cic[ch];
			c.i1 += (uint64_t)(int64_t)x[ch];
			c.i2 += c.i1;
			c.i3 += c.i2;
		}
		if (++g_cicPhase == kDecimation)
		{
			g_cicPhase = 0;
			if (g_audioFrames < kMaxOut)
			{
				g_audio[g_audioFrames * 2] = cic_out(g_cic[0]);
				g_audio[g_audioFrames * 2 + 1] = cic_out(g_cic[1]);
				g_audioFrames++;
			}
		}
	}
}

// n samples of every Game Boy, from the start of their buffers, into the mix
static void mix_sound(int n)
{
	memset(g_mixL, 0, n * sizeof g_mixL[0]);
	memset(g_mixR, 0, n * sizeof g_mixR[0]);
	for (int p = 0; p < g_count; p++)
	{
		const uint32_t *a = g_p[p].audio;
		for (int i = 0; i < n; i++)
		{
			// left in the low half, right in the high
			g_mixL[i] += ((int16_t)(a[i] & 0xFFFF) >> 1) / g_count;
			g_mixR[i] += ((int16_t)(a[i] >> 16) >> 1) / g_count;
		}
	}

	if (g_sgb)
	{
		std::size_t count = 0;
		const unsigned remainder = g_gb[0].generateSgbSamples(g_sgbAudio, count);
		// sample k is at 65 - remainder + 65k on this frame's timeline, as
		// BizHawk places it; one that falls after the frame's end starts the
		// next frame
		int64_t t = 65 - (int64_t)remainder;
		int at = 0;
		for (std::size_t k = 0; k <= count; k++, t += 65)
		{
			const int until = k < count ? (int)(t < 0 ? 0 : t > n ? n : t) : n;
			for (; at < until; at++)
			{
				g_mixL[at] += g_sgbHeld[0];
				g_mixR[at] += g_sgbHeld[1];
			}
			if (k < count)
			{
				g_sgbHeld[0] = g_sgbAudio[k * 2];
				g_sgbHeld[1] = g_sgbAudio[k * 2 + 1];
			}
		}
	}

	for (int p = 0; p < g_count; p++)
	{
		const unsigned mbc = g_gb[p].generateMbcSamples(g_mbcAudio);
		int32_t *held = g_mbcHeld[p];
		if (!mbc && !held[0] && !held[1])
			continue;
		for (int i = 0; i < n; i++)
		{
			if ((unsigned)i < mbc)
			{
				held[0] = g_mbcAudio[i * 2];
				held[1] = g_mbcAudio[i * 2 + 1];
			}
			g_mixL[i] += held[0] / g_count;
			g_mixR[i] += held[1] / g_count;
		}
	}

	decimate(n);
}

// ---- the controls ------------------------------------------------------------
// The Game Boy's eight buttons. The Super Game Boy reads up to four pads,
// one at a time, and says which (getJoypadIndex); a link machine has one
// pad for each Game Boy. Power turns a console off and on again, as
// BizHawk's Power does; a link machine has one for each Game Boy, and
// buttons that pull the cables out and put them back, and for three or four
// Game Boys change which of them are joined.
enum { kPadButtons = 8 };
static const char *const kPadNames[kPadButtons] = {
	"Up", "Down", "Left", "Right", "Start", "Select", "B", "A",
};
// libgambatte's bits for them
static const unsigned kPadBits[kPadButtons] = {
	0x40, 0x80, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01,
};
enum : int
{
	kPads = kMaxGb * kPadButtons,
	// one console: Power
	kPower = kPads,
	kSingleButtons,
	// a link machine: a Power for each, then the cables
	kLinkPower = kPads,
	kLinkConnection = kLinkPower + kMaxGb,
	kLinkShift,
	kLinkSpacing,
	kLinkButtons,
	kMaxButtons = kLinkButtons,
};
static char g_buttonNames[kMaxButtons][32];
static int g_buttonCount;
static bool g_held[kMaxButtons];
static int g_inputReads;
static bool g_inputRead;

// Chimera's function to call at each read of the buttons, so a Lua script's
// event.oninputpoll runs there (chimera#344). It is the same callback slot in
// every session and no savestate carries it.
ECL_INVISIBLE static uintptr_t (*g_inputPoll)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
ECL_EXPORT void SetInputPollCallback(uint64_t fn)
{
	g_inputPoll = (uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t))(uintptr_t)fn;
}

static unsigned input_getter(void *p)
{
	g_inputReads++;
	if (g_inputPoll)
		g_inputPoll(0, 0, 0, 0, 0, 0);
	const int i = (int)(intptr_t)p;
	if (!g_sgb)
		return g_p[i].padBits;
	// the Super Game Boy's SNES software never passes on Left with Right or
	// Up with Down: either pair held reads as neither, as BizHawk has it
	unsigned b = g_p[g_gb[0].getJoypadIndex() & 3].padBits;
	if ((b & 0x30) == 0x30)
		b &= ~0x30u;
	if ((b & 0xC0) == 0xC0)
		b &= ~0xC0u;
	return b;
}

static void set_controls(void)
{
	for (int p = 0; p < kMaxGb; p++)
	{
		unsigned bits = 0;
		for (int b = 0; b < kPadButtons; b++)
			if (g_held[p * kPadButtons + b])
				bits |= kPadBits[b];
		g_p[p].padBits = bits;
	}
}

// ---- files and settings ------------------------------------------------------
static void *read_file(const char *name, long *size)
{
	FILE *f = fopen(name, "rb");
	if (!f)
		return nullptr;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	void *data = n > 0 ? malloc((size_t)n) : nullptr;
	if (data && fread(data, 1, (size_t)n, f) != (size_t)n)
	{
		free(data);
		data = nullptr;
	}
	fclose(f);
	*size = n;
	return data;
}

static bool ends_with(const char *s, const char *suffix)
{
	const size_t n = strlen(s), m = strlen(suffix);
	return n >= m && !strcasecmp(s + n - m, suffix);
}

static int setting_str(const char *name, char *out, int size)
{
	return wbx_setting_str(name, out, size);
}

static Model model_named(const char *v)
{
	if (!strcmp(v, "GB"))
		return kGb;
	if (!strcmp(v, "GBC"))
		return kGbc;
	if (!strcmp(v, "GBA"))
		return kGba;
	if (!strcmp(v, "SGB2"))
		return kSgb2;
	return kAuto;
}

static bool boot_rom_setting(void)
{
	char v[8] = "on";
	setting_str("bootRom", v, sizeof v);
	return strcmp(v, "off") != 0;
}

// the slots of a Game Boy's game and saved data: the first's, then
// "game2", "savedata2" and so on
static void slot_id(char *out, size_t size, const char *base, int i)
{
	if (i == 0)
		snprintf(out, size, "%s", base);
	else
		snprintf(out, size, "%s%d", base, i + 1);
}

// How long a Game Boy waits after power-on before its processor starts, as
// libgambatte's load does: the Super Game Boy's SNES starts it after its
// own start-up, and a Game Boy Advance with its boot ROM first spends the
// time it takes to switch to the Game Boy Color's mode.
static std::size_t stall_samples(unsigned flags)
{
	if (flags & gambatte::GB::GBA_FLAG && !(flags & gambatte::GB::NO_BIOS))
		return 485808;
	if (flags & gambatte::GB::SGB_MODE)
		return 128 * (2 << 14);
	return 0;
}

// Loads Game Boy i: its game, the boot ROM of its model, its saved data.
static bool load_player(int i, Model model)
{
	Player &p = g_p[i];
	char slot[16];
	slot_id(slot, sizeof slot, "game", i);
	p.gameName[0] = '\0';
	if (wbx_slot_count(slot) > 0)
		wbx_slot_name(slot, 0, p.gameName, sizeof p.gameName);
	else if (i == 0)
	{
		// a game opened with no project is mounted under its own name
		FILE *f = fopen("rom.name", "rb");
		if (f)
		{
			size_t got = fread(p.gameName, 1, sizeof p.gameName - 1, f);
			p.gameName[got] = '\0';
			fclose(f);
		}
	}
	if (!p.gameName[0])
	{
		if (g_link)
			snprintf(g_loadError, sizeof g_loadError, "error.no_file_player" TEXT_ARG "%d", i + 1);
		else
			snprintf(g_loadError, sizeof g_loadError, "error.no_file");
		return false;
	}
	long size = 0;
	char *rom = (char *)read_file(p.gameName, &size);
	if (!rom)
	{
		snprintf(g_loadError, sizeof g_loadError, "error.not_read" TEXT_ARG "%s", p.gameName);
		return false;
	}
	if (size < 0x150)
	{
		free(rom);
		snprintf(g_loadError, sizeof g_loadError, "error.too_short" TEXT_ARG "%s" TEXT_ARG "%ld", p.gameName, size);
		return false;
	}

	// with no model given, the Game Boy Color for a .gbc file and the Game
	// Boy for anything else
	if (model == kAuto)
		model = ends_with(p.gameName, ".gbc") ? kGbc : kGb;
	p.model = model;
	p.loadFlags = gambatte::GB::READONLY_SAV;
	switch (model)
	{
	case kGbc: p.loadFlags |= gambatte::GB::CGB_MODE; break;
	case kGba: p.loadFlags |= gambatte::GB::CGB_MODE | gambatte::GB::GBA_FLAG; break;
	case kSgb2: p.loadFlags |= gambatte::GB::SGB_MODE; break;
	default: break;
	}

	// The boot ROM: the console's own start-up program, which shows the
	// logo and then starts the game. Without it, libgambatte starts the game
	// in the state the boot ROM would have left the machine in.
	if (boot_rom_setting())
	{
		const char *id = model == kGb ? "dmg_boot.bin"
			: model == kGbc ? "cgb_boot.bin"
			: model == kGba ? "agb_boot.bin"
			: "sgb2_boot.bin";
		long biosSize = 0;
		char *bios = (char *)read_file(id, &biosSize);
		const long want = model == kGb || model == kSgb2 ? 0x100 : 0x900;
		if (!bios || biosSize != want)
		{
			free(bios);
			free(rom);
			snprintf(g_loadError, sizeof g_loadError, "error.boot_rom" TEXT_ARG "%s" TEXT_ARG "%ld", id, want);
			return false;
		}
		g_gb[i].loadBios(bios, (std::size_t)biosSize);
		free(bios);
	}
	else
		p.loadFlags |= gambatte::GB::NO_BIOS;

	g_gb[i].setInputGetter(input_getter, (void *)(intptr_t)i);
	const gambatte::LoadRes res = g_gb[i].load(rom, (std::size_t)size, p.loadFlags);
	free(rom);
	if (res != gambatte::LOADRES_OK)
	{
		snprintf(g_loadError, sizeof g_loadError, "error.not_loaded" TEXT_ARG "%s" TEXT_ARG "%s", p.gameName,
			gambatte::to_string(res).c_str());
		return false;
	}
	// a cartridge's clock counts the machine's cycles, never the computer's
	g_gb[i].setTimeMode(true);

	// the cartridge's memory at the start: empty, or the project's save data
	slot_id(slot, sizeof slot, "savedata", i);
	if (wbx_slot_count(slot) > 0)
	{
		char name[256];
		const unsigned want = g_gb[i].getSavedataLength();
		if (want && wbx_slot_name(slot, 0, name, sizeof name))
		{
			long n = 0;
			char *saved = (char *)read_file(name, &n);
			if (saved)
			{
				// a shorter file (a .sav of another emulator without the
				// clock's registers) leaves the rest at zero
				char *buf = (char *)calloc(want, 1);
				memcpy(buf, saved, n < (long)want ? (size_t)n : want);
				g_gb[i].loadSavedata(buf);
				free(buf);
				free(saved);
			}
		}
	}
	return true;
}

// ---- the link cables ---------------------------------------------------------
// libgambatte's link calls (linkStatus): 256 asks whether a Game Boy clocked
// a byte out through its port, 257 says it was seen, 258 is the byte, and
// any value up to 255 is the byte that comes in; 259 to 263 are the same for
// the Game Boy Color's infrared light; 264 and 265 plug the cable in and
// pull it out.
static void link_pair(int one, int two)
{
	gambatte::GB &a = g_gb[one], &b = g_gb[two];
	if (a.linkStatus(256))
	{
		a.linkStatus(257);
		const int ao = a.linkStatus(258), bo = b.linkStatus(258);
		b.linkStatus(ao & 0xFF);
		a.linkStatus(bo & 0xFF);
	}
	if (b.linkStatus(256))
	{
		b.linkStatus(257);
		const int bo = b.linkStatus(258), ao = a.linkStatus(258);
		a.linkStatus(bo & 0xFF);
		b.linkStatus(ao & 0xFF);
	}
	if (a.linkStatus(259))
	{
		a.linkStatus(260);
		b.linkStatus(a.linkStatus(261) ? 262 : 263);
	}
	if (b.linkStatus(259))
	{
		b.linkStatus(260);
		a.linkStatus(b.linkStatus(261) ? 262 : 263);
	}
}

// Which Game Boys are joined, as BizHawk joins them. Two: P1 and P2. Three:
// P1 and P2; shifted, P2 and P3; spaced out, P1 and P3. Four: P1 with P2
// and P3 with P4; shifted, P2 with P3 and P4 with P1; spaced out, P1 with
// P3 and P2 with P4.
static void link_ports(void)
{
	if (g_count == 2)
		link_pair(0, 1);
	else if (g_count == 3)
	{
		if (g_spaced)
			link_pair(0, 2);
		else if (g_shifted)
			link_pair(1, 2);
		else
			link_pair(0, 1);
	}
	else if (g_count == 4)
	{
		if (g_spaced)
		{
			link_pair(0, 2);
			link_pair(1, 3);
		}
		else if (g_shifted)
		{
			link_pair(1, 2);
			link_pair(3, 0);
		}
		else
		{
			link_pair(0, 1);
			link_pair(2, 3);
		}
	}
}

static void set_connected(bool on)
{
	g_connected = on;
	for (int i = 0; i < g_count; i++)
		g_gb[i].linkStatus(on ? 264 : 265);
}

// a toggle: pressed (held this frame and not the last), it changes
static bool pressed(int button, bool &wasHeld)
{
	const bool now = g_held[button];
	const bool edge = now && !wasHeld;
	wasHeld = now;
	return edge;
}

// One frame of a link machine: every Game Boy runs 35,112 samples, the
// whole frame together, kLinkStep samples at a time while the cables are
// in, with the link ports looked at in between. A Game Boy that ran past a
// step keeps its lead; one that ran past the frame's end starts the next
// frame with those samples.
static void link_frame(void)
{
	int n[kMaxGb];
	for (int i = 0; i < g_count; i++)
		n[i] = g_p[i].carried;
	const int step = g_connected ? kLinkStep : (int)kSamplesPerFrame;
	for (int target = 0; target < (int)kSamplesPerFrame;)
	{
		target += step;
		if (target > (int)kSamplesPerFrame)
			target = kSamplesPerFrame;
		for (int i = 0; i < g_count; i++)
		{
			// runFor stops early at the end of a picture: on until the target
			while (n[i] < target)
			{
				std::size_t samples = (std::size_t)(target - n[i]);
				if (g_gb[i].runFor(g_p[i].draw, kGbWidth, g_p[i].audio + n[i], samples) >= 0)
					show(i);
				n[i] += (int)samples;
			}
		}
		if (g_connected)
			link_ports();
	}
	mix_sound(kSamplesPerFrame);
	for (int i = 0; i < g_count; i++)
	{
		const int carried = n[i] - (int)kSamplesPerFrame;
		memmove(g_p[i].audio, g_p[i].audio + kSamplesPerFrame, carried * sizeof(uint32_t));
		g_p[i].carried = carried;
	}
}

// ---- Chimera's calls -----------------------------------------------------------
extern "C" {

ECL_EXPORT const char *GetLoadError(void) { return g_loadError; }
ECL_EXPORT const char *DescribeDips(void) { return ""; }

// For the New Project window: each linked Game Boy's model from its game,
// the Game Boy Color for a .gbc file and the Game Boy for anything else.
static char g_suggestion[512];
ECL_EXPORT const char *SuggestSettings(void)
{
	char values[256] = "";
	int got = 0;
	// Chimera asks before the settings are known, with the files only: a
	// project with a game for P2 is a link machine's
	const int count = wbx_slot_count("game2") > 0 ? kMaxGb : 0;
	for (int i = 0; i < count; i++)
	{
		char slot[16], name[256];
		slot_id(slot, sizeof slot, "game", i);
		if (wbx_slot_count(slot) <= 0 || !wbx_slot_name(slot, 0, name, sizeof name))
			continue;
		snprintf(values + strlen(values), sizeof values - strlen(values), "%s\"linkModel%d\":\"%s\"",
			got++ ? "," : "", i + 1, ends_with(name, ".gbc") ? "GBC" : "GB");
	}
	snprintf(g_suggestion, sizeof g_suggestion,
		"{\"values\":{%s},\"note\":\"%s\",\"firmware\":[]}", values,
		got ? "suggest.link_models" : "");
	return g_suggestion;
}

ECL_EXPORT int Init(void)
{
	g_loadError[0] = '\0';
	char v[16] = "";
	setting_str("model", v, sizeof v);
	g_count = !strcmp(v, "LINK2") ? 2 : !strcmp(v, "LINK3") ? 3 : !strcmp(v, "LINK4") ? 4 : 1;
	g_link = g_count > 1;
	if (g_link)
	{
		for (int i = 0; i < g_count; i++)
		{
			char name[16], m[8] = "GB";
			snprintf(name, sizeof name, "linkModel%d", i + 1);
			setting_str(name, m, sizeof m);
			Model model = model_named(m);
			if (model == kSgb2)
			{
				snprintf(g_loadError, sizeof g_loadError, "error.sgb_link");
				return 0;
			}
			if (!load_player(i, model == kAuto ? kGb : model))
				return 0;
		}
		set_connected(true);
	}
	else if (!load_player(0, model_named(v)))
		return 0;
	g_sgb = !g_link && g_p[0].model == kSgb2;

	if (g_link)
	{
		for (int p = 0; p < kMaxGb; p++)
		{
			for (int b = 0; b < kPadButtons; b++)
				snprintf(g_buttonNames[p * kPadButtons + b], sizeof g_buttonNames[0], "P%d %s", p + 1, kPadNames[b]);
			snprintf(g_buttonNames[kLinkPower + p], sizeof g_buttonNames[0], "P%d Power", p + 1);
		}
		snprintf(g_buttonNames[kLinkConnection], sizeof g_buttonNames[0], "Toggle Link Connection");
		snprintf(g_buttonNames[kLinkShift], sizeof g_buttonNames[0], "Toggle Link Shift");
		snprintf(g_buttonNames[kLinkSpacing], sizeof g_buttonNames[0], "Toggle Link Spacing");
		g_buttonCount = kLinkButtons;
	}
	else
	{
		for (int p = 0; p < kMaxGb; p++)
			for (int b = 0; b < kPadButtons; b++)
				snprintf(g_buttonNames[p * kPadButtons + b], sizeof g_buttonNames[0], "P%d %s", p + 1, kPadNames[b]);
		snprintf(g_buttonNames[kPower], sizeof g_buttonNames[0], "Power");
		g_buttonCount = kSingleButtons;
	}

	memset(g_picture, 0, sizeof g_picture);
	g_frameCount = 0;
	return 1;
}

ECL_EXPORT int GetButtonCount(void) { return g_buttonCount; }
ECL_EXPORT const char *GetButtonName(int32_t i) { return i >= 0 && i < g_buttonCount ? g_buttonNames[i] : ""; }
ECL_EXPORT int IsButtonActive(int32_t i)
{
	if (i < 0 || i >= g_buttonCount)
		return 0;
	if (i < kPads)
	{
		// a pad is live for each linked Game Boy; on one console, pads 2
		// to 4 are the Super Game Boy's
		const int pad = i / kPadButtons;
		return g_link ? pad < g_count : pad == 0 || g_sgb;
	}
	if (!g_link)
		return 1;
	if (i < kLinkConnection)
		return i - kLinkPower < g_count;
	if (i == kLinkConnection)
		return 1;
	// which are joined matters with three or four
	return g_count > 2;
}
ECL_EXPORT void SetButton(int32_t i, int32_t state)
{
	if (i >= 0 && i < g_buttonCount)
		g_held[i] = state != 0;
}

ECL_EXPORT int GetAxisCount(void) { return 0; }
ECL_EXPORT const char *GetAxisName(int32_t) { return ""; }
ECL_EXPORT int IsAxisActive(int32_t) { return 0; }
ECL_EXPORT void SetAxis(int32_t, int32_t) {}

ECL_EXPORT void FrameAdvance(uint64_t packed)
{
	(void)packed;
	g_audioFrames = 0;
	g_inputReads = 0;

	// Power: pressed, a console starts again from power-on
	for (int i = 0; i < g_count; i++)
		if (pressed(g_link ? kLinkPower + i : (int)kPower, g_p[i].powerWasHeld))
			g_gb[i].reset(stall_samples(g_p[i].loadFlags));
	set_controls();

	if (g_link)
	{
		if (pressed(kLinkConnection, g_connWasHeld))
			set_connected(!g_connected);
		if (g_count > 2)
		{
			if (pressed(kLinkShift, g_shiftWasHeld))
				g_shifted = !g_shifted;
			if (pressed(kLinkSpacing, g_spaceWasHeld))
				g_spaced = !g_spaced;
		}
		link_frame();
	}
	else
	{
		// one call: it ends at the end of a picture, or after a frame's worth
		// of samples when the screen is off, as BizHawk's frames do
		std::size_t samples = kSamplesPerFrame;
		if (g_gb[0].runFor(g_p[0].draw, kGbWidth, g_p[0].audio, samples) >= 0)
		{
			if (g_sgb)
				show(0);
			else if (g_render)
				memcpy(g_picture, g_p[0].draw, sizeof g_p[0].draw);
		}
		mix_sound((int)samples);
	}

	g_frameCount++;
	g_inputRead = g_inputReads > 0;
}

ECL_EXPORT int InputWasRead(void) { return g_inputRead; }
ECL_EXPORT void SetRenderingEnabled(int on) { g_render = on != 0; }

ECL_EXPORT uint32_t *GetVideoBgra(void) { return g_picture; }
ECL_EXPORT int GetVideoWidth(void) { return picture_width(); }
ECL_EXPORT int GetVideoHeight(void) { return picture_height(); }
ECL_EXPORT int GetDisplayAspectX(void) { return picture_width(); }
ECL_EXPORT int GetDisplayAspectY(void) { return picture_height(); }
// 4,194,304 cycles a second over 70,224 a frame
ECL_EXPORT int GetVsyncNumerator(void) { return 4194304; }
ECL_EXPORT int GetVsyncDenominator(void) { return 70224; }

ECL_EXPORT int16_t *GetAudio(void) { return g_audio; }
ECL_EXPORT int GetAudioSampleCount(void) { return g_audioFrames; }

// The memory areas libgambatte shows: the cartridge's ROM (read only), the
// video memory, the work memory, the cartridge's memory, the sprite table
// (OAM) and the processor's high memory (HRAM). The Game Boy Color has
// twice the video memory and four times the work memory. A link machine
// has each Game Boy's, named with its player: "P2 WRAM".
enum { kRom = 1, kVram = 0, kWram = 2, kCartRam = 3, kOam = 4, kHram = 5 };
static const struct { const char *name; int area; } kDomains[] = {
	{ "ROM", kRom }, { "VRAM", kVram }, { "WRAM", kWram },
	{ "CartRAM", kCartRam }, { "OAM", kOam }, { "HRAM", kHram },
};
enum { kDomainsEach = sizeof kDomains / sizeof kDomains[0] };
static char g_domainNames[kMaxGb * kDomainsEach][24];

static bool area(int i, unsigned char **data, int *length)
{
	*data = nullptr;
	*length = 0;
	if (i < 0 || i >= g_count * kDomainsEach)
		return false;
	return g_gb[i / kDomainsEach].getMemoryArea(kDomains[i % kDomainsEach].area, data, length);
}

ECL_EXPORT int GetMemoryDomainCount(void) { return g_count * kDomainsEach; }
ECL_EXPORT const char *GetMemoryDomainName(int i)
{
	if (i < 0 || i >= g_count * kDomainsEach)
		return "";
	if (!g_link)
		return kDomains[i].name;
	snprintf(g_domainNames[i], sizeof g_domainNames[i], "P%d %s", i / kDomainsEach + 1, kDomains[i % kDomainsEach].name);
	return g_domainNames[i];
}
ECL_EXPORT uint8_t *GetMemoryDomainPtr(int i)
{
	unsigned char *d;
	int n;
	return area(i, &d, &n) && n > 0 ? d : nullptr;
}
ECL_EXPORT int64_t GetMemoryDomainSize(int i)
{
	unsigned char *d;
	int n;
	return area(i, &d, &n) ? n : 0;
}
ECL_EXPORT int GetMemoryDomainWritable(int i)
{
	return i >= 0 && i < g_count * kDomainsEach && kDomains[i % kDomainsEach].area != kRom;
}

// The system bus: the 64 KB a Game Boy's processor sees, read as a
// debugger reads it (libgambatte's peek, which changes nothing in the
// machine); one for each Game Boy of a link machine.
static const int64_t kBusRanges[] = { 0, 0x10000, 0, 0 };
ECL_INVISIBLE static uint8_t g_busBuf[0x10000];
static char g_busNames[kMaxGb][24];
ECL_EXPORT int32_t GetBusCount(void) { return g_count; }
ECL_EXPORT const char *GetBusName(int32_t bus)
{
	if (bus < 0 || bus >= g_count)
		return "";
	if (!g_link)
		return "System Bus";
	snprintf(g_busNames[bus], sizeof g_busNames[bus], "P%d System Bus", bus + 1);
	return g_busNames[bus];
}
ECL_EXPORT int64_t GetBusSize(int32_t bus) { return bus >= 0 && bus < g_count ? 0x10000 : 0; }
ECL_EXPORT int32_t GetBusWritable(int32_t bus) { return bus >= 0 && bus < g_count; }
ECL_EXPORT int32_t PeekBus(int32_t bus, int32_t addr)
{
	if (bus < 0 || bus >= g_count || addr < 0 || addr >= 0x10000)
		return 0;
	return g_gb[bus].externalRead((unsigned short)addr);
}
ECL_EXPORT const int64_t *GetBusRanges(int32_t bus) { return bus >= 0 && bus < g_count ? kBusRanges : nullptr; }
ECL_EXPORT const uint8_t *ReadBus(int32_t bus, int64_t addr, int32_t len)
{
	if (bus < 0 || bus >= g_count || addr < 0 || len < 0 || addr + len > 0x10000)
		return nullptr;
	for (int32_t i = 0; i < len; i++)
		g_busBuf[i] = g_gb[bus].externalRead((unsigned short)(addr + i));
	return g_busBuf;
}
ECL_EXPORT void PokeBus(int32_t bus, int32_t addr, int32_t value)
{
	if (bus >= 0 && bus < g_count && addr >= 0 && addr < 0x10000)
		g_gb[bus].externalWrite((unsigned short)addr, (unsigned char)value);
}

// Export Save Data: each cartridge's battery-backed memory, and after it the
// clock's registers for a cartridge with a clock, as Gambatte lays them
// out: the game's name with .sav, and on a link machine "P2 " before it.
static char g_saveName[256 + 16];
// the largest: 128 KB of cartridge memory, then a HuC3's clock (8 + 260 bytes)
static uint8_t g_saveBuf[0x20000 + 8 + 0x104];

static bool has_save(int i)
{
	const unsigned n = g_gb[i].getSavedataLength();
	return n && n <= sizeof g_saveBuf;
}
// the Game Boy of the i-th file
static int save_owner(int32_t i)
{
	for (int p = 0; p < g_count; p++)
		if (has_save(p) && i-- == 0)
			return p;
	return -1;
}
ECL_EXPORT int32_t GetSaveDataFileCount(void)
{
	int n = 0;
	for (int p = 0; p < g_count; p++)
		n += has_save(p);
	return n;
}
ECL_EXPORT const char *GetSaveDataFileName(int32_t i)
{
	const int p = save_owner(i);
	if (p < 0)
		return "";
	char base[256];
	snprintf(base, sizeof base, "%s", g_p[p].gameName);
	char *dot = strrchr(base, '.');
	if (dot)
		*dot = '\0';
	if (g_link)
		snprintf(g_saveName, sizeof g_saveName, "P%d %s.sav", p + 1, base);
	else
		snprintf(g_saveName, sizeof g_saveName, "%s.sav", base);
	return g_saveName;
}
ECL_EXPORT int64_t GetSaveDataFileSize(int32_t i)
{
	const int p = save_owner(i);
	return p < 0 ? 0 : g_gb[p].getSavedataLength();
}
ECL_EXPORT const uint8_t *GetSaveDataFileBuffer(int32_t i)
{
	const int p = save_owner(i);
	if (p < 0)
		return nullptr;
	g_gb[p].saveSavedata((char *)g_saveBuf);
	return g_saveBuf;
}

} // extern "C"
