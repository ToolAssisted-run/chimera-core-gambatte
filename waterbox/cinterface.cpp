// cinterface.cpp - Gambatte as a Chimera core: the functions Chimera calls.
//
// The machine is libgambatte from Gambatte-Speedrun (gambatte-core), the
// Game Boy and Game Boy Color emulator speedrunners use. Its own programs
// around it (a Qt window, a test runner) are not built; this file takes
// their place inside the sandbox. It loads the game, the boot ROM and the
// saved data, gives the machine the controls, runs it to the end of a
// picture, and turns its sound into a rate Chimera can play.
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
static char g_loadError[512];

// ---- the machine -------------------------------------------------------------
static gambatte::GB g_gb;
static char g_gameName[256];
static unsigned g_loadFlags;
static bool g_sgb;
static uint64_t g_frameCount;

// The models a game can run on, as the Model setting names them.
enum Model { kAuto, kGb, kGbc, kGba, kSgb2 };

// A frame is 70,224 cycles of the 4,194,304 Hz clock (59.73 a second); the
// machine's sound comes as one stereo sample every two cycles, 35,112 a
// frame. runFor stops at the end of each picture, and may run up to 2,064
// samples past what it was asked for.
enum : unsigned
{
	kSamplesPerFrame = 35112,
	kSampleOverrun = 2064,
};

// ---- the picture -------------------------------------------------------------
// The Game Boy's screen is 160 by 144. The Super Game Boy draws it inside a
// 256 by 224 border. The machine draws into g_draw as it goes; a finished
// picture is copied to g_picture, which is what Chimera reads, so a frame
// that ends with the screen off shows the last finished picture.
enum { kGbWidth = 160, kGbHeight = 144, kSgbWidth = 256, kSgbHeight = 224 };
static uint32_t g_draw[kGbWidth * kGbHeight];
static uint32_t g_picture[kSgbWidth * kSgbHeight];
static bool g_render = true;

// ---- the sound -------------------------------------------------------------
// The machine's own sound is 2,097,152 samples a second. The Super Game
// Boy's SNES sound chip adds one sample every 65 of those, and a HuC3
// cartridge's speaker one for each. All three are added on the machine's
// timeline, as BizHawk does (the Game Boy's at half its level), and brought
// down to 32,768 samples a second - one in 64 - through a third-order CIC
// filter: three running sums, one sample in 64 kept, three differences.
// It is integer arithmetic, so every computer gives the same samples, and
// it removes what would otherwise fold back into the audible range.
enum { kDecimation = 64, kCicShift = 18 }; // 64^3 = 2^18
enum { kMaxIn = kSamplesPerFrame + kSampleOverrun, kMaxOut = kMaxIn / kDecimation + 2 };
static uint32_t g_gbAudio[kMaxIn];
static short g_sgbAudio[2048 * 2];
static short g_mbcAudio[kMaxIn * 2];
static int32_t g_mixL[kMaxIn], g_mixR[kMaxIn];
static int16_t g_audio[kMaxOut * 2];
static int g_audioFrames;
static int32_t g_sgbHeld[2], g_mbcHeld[2];

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

static void mix_sound(int n)
{
	for (int i = 0; i < n; i++)
	{
		// the machine's sample: left in the low half, right in the high
		g_mixL[i] = (int16_t)(g_gbAudio[i] & 0xFFFF) >> 1;
		g_mixR[i] = (int16_t)(g_gbAudio[i] >> 16) >> 1;
	}

	if (g_sgb)
	{
		std::size_t count = 0;
		const unsigned remainder = g_gb.generateSgbSamples(g_sgbAudio, count);
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

	const unsigned mbc = g_gb.generateMbcSamples(g_mbcAudio);
	if (mbc || g_mbcHeld[0] || g_mbcHeld[1])
	{
		for (int i = 0; i < n; i++)
		{
			if ((unsigned)i < mbc)
			{
				g_mbcHeld[0] = g_mbcAudio[i * 2];
				g_mbcHeld[1] = g_mbcAudio[i * 2 + 1];
			}
			g_mixL[i] += g_mbcHeld[0];
			g_mixR[i] += g_mbcHeld[1];
		}
	}

	decimate(n);
}

// ---- the controls ------------------------------------------------------------
// The Game Boy's eight buttons; the Super Game Boy reads up to four pads,
// one at a time, and says which (getJoypadIndex). Power turns the machine
// off and on again, as BizHawk's Power does.
enum { kPadButtons = 8, kPads = 4 };
static const char *const kPadNames[kPadButtons] = {
	"Up", "Down", "Left", "Right", "Start", "Select", "B", "A",
};
// libgambatte's bits for them
static const unsigned kPadBits[kPadButtons] = {
	0x40, 0x80, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01,
};
enum : int
{
	kPower = kPads * kPadButtons,
	kButtonCount,
};
static char g_buttonNames[kButtonCount][16];
static bool g_held[kButtonCount];
static bool g_powerWasHeld;
static unsigned g_padBits[kPads];
static int g_inputReads;
static bool g_inputRead;

static unsigned input_getter(void *)
{
	g_inputReads++;
	if (!g_sgb)
		return g_padBits[0];
	// the Super Game Boy's SNES software never passes on Left with Right or
	// Up with Down: either pair held reads as neither, as BizHawk has it
	unsigned b = g_padBits[g_gb.getJoypadIndex() & 3];
	if ((b & 0x30) == 0x30)
		b &= ~0x30u;
	if ((b & 0xC0) == 0xC0)
		b &= ~0xC0u;
	return b;
}

static void set_controls(void)
{
	for (int p = 0; p < kPads; p++)
	{
		unsigned bits = 0;
		for (int b = 0; b < kPadButtons; b++)
			if (g_held[p * kPadButtons + b])
				bits |= kPadBits[b];
		g_padBits[p] = bits;
	}
}

// ---- files -------------------------------------------------------------------
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

static Model model_setting(void)
{
	char v[16] = "";
	setting_str("model", v, sizeof v);
	if (!strcmp(v, "GB"))
		return kGb;
	if (!strcmp(v, "GBC"))
		return kGbc;
	if (!strcmp(v, "GBA"))
		return kGba;
	if (!strcmp(v, "SGB2"))
		return kSgb2;
	// Chimera always gives the Model; a run with none (the test runners'
	// own) goes by the file's extension, as Chimera chooses the machine for
	// a game opened with no project
	return kAuto;
}

static bool boot_rom_setting(void)
{
	char v[8] = "on";
	setting_str("bootRom", v, sizeof v);
	return strcmp(v, "off") != 0;
}

// How long the machine waits after power-on before its processor starts,
// as libgambatte's load does: the Super Game Boy's SNES starts it after its
// own start-up, and a Game Boy Advance with its boot ROM first spends the
// time it takes to switch to the Game Boy Color's mode.
static std::size_t stall_samples(void)
{
	if (g_loadFlags & gambatte::GB::GBA_FLAG && !(g_loadFlags & gambatte::GB::NO_BIOS))
		return 485808;
	if (g_loadFlags & gambatte::GB::SGB_MODE)
		return 128 * (2 << 14);
	return 0;
}

// ---- Chimera's calls -----------------------------------------------------------
extern "C" {

ECL_EXPORT const char *GetLoadError(void) { return g_loadError; }
ECL_EXPORT const char *SuggestSettings(void) { return "{\"values\":{},\"note\":\"\",\"firmware\":[]}"; }
ECL_EXPORT const char *DescribeDips(void) { return ""; }

ECL_EXPORT int Init(void)
{
	g_loadError[0] = '\0';
	g_gameName[0] = '\0';
	if (wbx_slot_count("game") > 0)
		wbx_slot_name("game", 0, g_gameName, sizeof g_gameName);
	else
	{
		// a game opened with no project is mounted under its own name
		FILE *f = fopen("rom.name", "rb");
		if (f)
		{
			size_t got = fread(g_gameName, 1, sizeof g_gameName - 1, f);
			g_gameName[got] = '\0';
			fclose(f);
		}
	}
	if (!g_gameName[0])
	{
		snprintf(g_loadError, sizeof g_loadError, "no game was given");
		return 0;
	}
	long size = 0;
	char *rom = (char *)read_file(g_gameName, &size);
	if (!rom)
	{
		snprintf(g_loadError, sizeof g_loadError, "the game %s could not be read", g_gameName);
		return 0;
	}
	if (size < 0x150)
	{
		free(rom);
		snprintf(g_loadError, sizeof g_loadError, "%s is too short to be a Game Boy game (%ld bytes)", g_gameName, size);
		return 0;
	}

	// The model: the setting's, or with none, the Game Boy Color for a .gbc
	// file and the Game Boy for anything else.
	Model model = model_setting();
	if (model == kAuto)
		model = ends_with(g_gameName, ".gbc") ? kGbc : kGb;
	g_loadFlags = gambatte::GB::READONLY_SAV;
	switch (model)
	{
	case kGbc: g_loadFlags |= gambatte::GB::CGB_MODE; break;
	case kGba: g_loadFlags |= gambatte::GB::CGB_MODE | gambatte::GB::GBA_FLAG; break;
	case kSgb2: g_loadFlags |= gambatte::GB::SGB_MODE; break;
	default: break;
	}
	g_sgb = model == kSgb2;

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
			snprintf(g_loadError, sizeof g_loadError,
				"the boot ROM %s is missing or is not %ld bytes; give it, or set Boot ROM to off", id, want);
			return 0;
		}
		g_gb.loadBios(bios, (std::size_t)biosSize);
		free(bios);
	}
	else
		g_loadFlags |= gambatte::GB::NO_BIOS;

	g_gb.setInputGetter(input_getter, nullptr);
	const gambatte::LoadRes res = g_gb.load(rom, (std::size_t)size, g_loadFlags);
	free(rom);
	if (res != gambatte::LOADRES_OK)
	{
		snprintf(g_loadError, sizeof g_loadError, "%s could not be loaded: %s", g_gameName,
			gambatte::to_string(res).c_str());
		return 0;
	}
	// a cartridge's clock counts the machine's cycles, never the computer's
	g_gb.setTimeMode(true);

	// the cartridge's memory at the start: empty, or the project's save data
	if (wbx_slot_count("savedata") > 0)
	{
		char name[256];
		const unsigned want = g_gb.getSavedataLength();
		if (want && wbx_slot_name("savedata", 0, name, sizeof name))
		{
			long n = 0;
			char *saved = (char *)read_file(name, &n);
			if (saved)
			{
				// a shorter file (a .sav of another emulator without the
				// clock's registers) leaves the rest at zero
				char *buf = (char *)calloc(want, 1);
				memcpy(buf, saved, n < (long)want ? (size_t)n : want);
				g_gb.loadSavedata(buf);
				free(buf);
				free(saved);
			}
		}
	}

	for (int p = 0; p < kPads; p++)
		for (int b = 0; b < kPadButtons; b++)
			snprintf(g_buttonNames[p * kPadButtons + b], sizeof g_buttonNames[0], "P%d %s", p + 1, kPadNames[b]);
	snprintf(g_buttonNames[kPower], sizeof g_buttonNames[0], "Power");

	memset(g_picture, 0, sizeof g_picture);
	g_frameCount = 0;
	return 1;
}

ECL_EXPORT int GetButtonCount(void) { return kButtonCount; }
ECL_EXPORT const char *GetButtonName(int32_t i) { return i >= 0 && i < kButtonCount ? g_buttonNames[i] : ""; }
ECL_EXPORT int IsButtonActive(int32_t i)
{
	if (i < 0 || i >= kButtonCount)
		return 0;
	// pads 2 to 4 are the Super Game Boy's
	if (i >= kPadButtons && i < kPower)
		return g_sgb;
	return 1;
}
ECL_EXPORT void SetButton(int32_t i, int32_t state)
{
	if (i >= 0 && i < kButtonCount)
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

	// Power: pressed, the machine starts again from power-on
	if (g_held[kPower] && !g_powerWasHeld)
		g_gb.reset(stall_samples());
	g_powerWasHeld = g_held[kPower];

	set_controls();

	// one call: it ends at the end of a picture, or after a frame's worth
	// of samples when the screen is off, as BizHawk's frames do
	std::size_t samples = kSamplesPerFrame;
	const std::ptrdiff_t drawn = g_gb.runFor(g_draw, kGbWidth, g_gbAudio, samples);
	if (drawn >= 0 && g_render)
	{
		if (g_sgb)
			g_gb.updateScreenBorder(g_picture, kSgbWidth);
		else
			memcpy(g_picture, g_draw, sizeof g_draw);
	}
	mix_sound((int)samples);

	g_frameCount++;
	g_inputRead = g_inputReads > 0;
}

ECL_EXPORT int InputWasRead(void) { return g_inputRead; }
ECL_EXPORT void SetRenderingEnabled(int on) { g_render = on != 0; }

ECL_EXPORT uint32_t *GetVideoBgra(void) { return g_picture; }
ECL_EXPORT int GetVideoWidth(void) { return g_sgb ? kSgbWidth : kGbWidth; }
ECL_EXPORT int GetVideoHeight(void) { return g_sgb ? kSgbHeight : kGbHeight; }
ECL_EXPORT int GetDisplayAspectX(void) { return GetVideoWidth(); }
ECL_EXPORT int GetDisplayAspectY(void) { return GetVideoHeight(); }
// 4,194,304 cycles a second over 70,224 a frame
ECL_EXPORT int GetVsyncNumerator(void) { return 4194304; }
ECL_EXPORT int GetVsyncDenominator(void) { return 70224; }

ECL_EXPORT int16_t *GetAudio(void) { return g_audio; }
ECL_EXPORT int GetAudioSampleCount(void) { return g_audioFrames; }

// The memory areas libgambatte shows: the cartridge's ROM (read only), the
// video memory, the work memory, the cartridge's memory, the sprite table
// (OAM) and the processor's high memory (HRAM). The Game Boy Color has
// twice the video memory and four times the work memory.
enum { kRom = 1, kVram = 0, kWram = 2, kCartRam = 3, kOam = 4, kHram = 5 };
static const struct { const char *name; int area; } kDomains[] = {
	{ "ROM", kRom }, { "VRAM", kVram }, { "WRAM", kWram },
	{ "CartRAM", kCartRam }, { "OAM", kOam }, { "HRAM", kHram },
};
enum { kDomainCount = sizeof kDomains / sizeof kDomains[0] };

static bool area(int i, unsigned char **data, int *length)
{
	*data = nullptr;
	*length = 0;
	return i >= 0 && i < kDomainCount && g_gb.getMemoryArea(kDomains[i].area, data, length);
}

ECL_EXPORT int GetMemoryDomainCount(void) { return kDomainCount; }
ECL_EXPORT const char *GetMemoryDomainName(int i) { return i >= 0 && i < kDomainCount ? kDomains[i].name : ""; }
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
ECL_EXPORT int GetMemoryDomainWritable(int i) { return i > 0 && i < kDomainCount; }

// The system bus: the 64 KB the processor sees, read as a debugger reads
// it (libgambatte's peek, which changes nothing in the machine).
static const int64_t kBusRanges[] = { 0, 0x10000, 0, 0 };
static uint8_t g_busBuf[0x10000];
ECL_EXPORT const int64_t *GetBusRanges(int32_t bus) { return bus == 0 ? kBusRanges : nullptr; }
ECL_EXPORT const uint8_t *ReadBus(int32_t bus, int64_t addr, int32_t len)
{
	if (bus != 0 || addr < 0 || len < 0 || addr + len > 0x10000)
		return nullptr;
	for (int32_t i = 0; i < len; i++)
		g_busBuf[i] = g_gb.externalRead((unsigned short)(addr + i));
	return g_busBuf;
}
ECL_EXPORT void PokeBus(int32_t bus, int32_t addr, int32_t value)
{
	if (bus == 0 && addr >= 0 && addr < 0x10000)
		g_gb.externalWrite((unsigned short)addr, (unsigned char)value);
}

// Export Save Data: the cartridge's battery-backed memory, and after it the
// clock's registers for a cartridge with a clock, as Gambatte lays them out.
static char g_saveName[256 + 8];
// the largest: 128 KB of cartridge memory, then a HuC3's clock (8 + 260 bytes)
static uint8_t g_saveBuf[0x20000 + 8 + 0x104];
ECL_EXPORT int32_t GetSaveDataFileCount(void)
{
	const unsigned n = g_gb.getSavedataLength();
	return n && n <= sizeof g_saveBuf ? 1 : 0;
}
ECL_EXPORT const char *GetSaveDataFileName(int32_t i)
{
	if (i != 0)
		return "";
	// the game's name with .sav, as Gambatte names it
	snprintf(g_saveName, sizeof g_saveName, "%s", g_gameName);
	char *dot = strrchr(g_saveName, '.');
	if (dot)
		*dot = '\0';
	strcat(g_saveName, ".sav");
	return g_saveName;
}
ECL_EXPORT int64_t GetSaveDataFileSize(int32_t i) { return i == 0 && GetSaveDataFileCount() ? g_gb.getSavedataLength() : 0; }
ECL_EXPORT const uint8_t *GetSaveDataFileBuffer(int32_t i)
{
	if (i != 0 || !GetSaveDataFileCount())
		return nullptr;
	g_gb.saveSavedata((char *)g_saveBuf);
	return g_saveBuf;
}

} // extern "C"
