// SPDX-License-Identifier: GPL-3.0-or-later
// BotW VR (docs/bettervr-ps5): the PS VR2 Sense controllers in the launcher's menus, next to the
// DualSense (ui/input.cpp). Each Sense controller's button word has the DualSense's bits (Cross,
// Circle, Square, Triangle, L1/R1, L2/R2, L3/R3, Options, Create: third_party/hmd2/reference/pad_input.h),
// and as it has no d-pad, its stick moves the focus. They are opened as the OpenXR runtime opens
// them (port/bettervr/xr/runtime.cpp: scePad types 3 and 4, read with scePadVrControllerReadState);
// the runtime takes the same handles over with scePadGetHandle when the game starts.

#include "../ps5/pad.h"

#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

void PS5Cemu_LogLine(std::string_view line);

extern "C"
{
	int scePadOpen(int user, int type, int index, const void* parameters);
	int scePadGetHandle(int user, int type, int index);
	int scePadVrControllerReadState(int handle, void* state);
}

namespace
{
	constexpr int kPadType[2] = {3, 4}; // left, right
	constexpr size_t kStateBytes = 0x68, kConnected = 0x40, kStickX = 0x04, kStickY = 0x05;
	constexpr auto kRetry = std::chrono::seconds(2);

	// a line of the boot log, with the "[vr]" the launcher's BetterVR lines carry
	void Log(const char* format, ...)
	{
		char text[256];
		va_list args;
		va_start(args, format);
		std::vsnprintf(text, sizeof(text), format, args);
		va_end(args);
		PS5Cemu_LogLine(std::string("[vr] ") + text);
	}

	struct Hand
	{
		int handle = -1;
		std::chrono::steady_clock::time_point nextOpen{};
		bool reportedMissing = false;
		bool reportedRead = false;
	};
	Hand s_hands[2];
}

// The buttons both Sense controllers hold, in ps5pad's bits, their sticks as the d-pad; false when
// neither is there. Called from the launcher's thread only.
bool PS5Cemu_SenseMenuButtons(uint32_t& buttons)
{
	buttons = 0;
	bool any = false;
	const auto now = std::chrono::steady_clock::now();
	const int user = ps5pad::UserId(0);
	if (user < 0)
		return false;
	for (int i = 0; i < 2; i++)
	{
		Hand& hand = s_hands[i];
		if (hand.handle < 0)
		{
			if (now < hand.nextOpen)
				continue;
			hand.nextOpen = now + kRetry;
			const int opened = scePadOpen(user, kPadType[i], 0, nullptr);
			int found = 0;
			int handle = opened;
			if (handle < 0)
			{
				found = scePadGetHandle(user, kPadType[i], 0);
				handle = found;
			}
			if (handle < 0)
			{
				// once per hand: the return codes say whether the console has them at all outside VR
				if (!hand.reportedMissing)
					Log("no %s Sense controller for the menus (user 0x%08x): scePadOpen -> 0x%08x, scePadGetHandle -> 0x%08x; trying every 2 s",
						i == 0 ? "left" : "right", (unsigned)user, (unsigned)opened, (unsigned)found);
				hand.reportedMissing = true;
				continue;
			}
			hand.reportedMissing = false;
			hand.handle = handle;
			Log("the %s Sense controller opened for the menus (handle %d)", i == 0 ? "left" : "right", handle);
		}
		alignas(16) uint8_t state[kStateBytes] = {};
		const int read = scePadVrControllerReadState(hand.handle, state);
		if (read != 0 || (state[kConnected] & 1) == 0)
		{
			// once per hand, so a pad that is still waking up does not repeat this every frame
			if (!hand.reportedRead)
				Log("the %s Sense controller's state cannot be read yet (0x%08x, connected %d)", i == 0 ? "left" : "right",
					(unsigned)read, (int)(state[kConnected] & 1));
			hand.reportedRead = true;
			continue;
		}
		hand.reportedRead = false;
		uint32_t word = 0;
		std::memcpy(&word, state, sizeof(word));
		if (word & ps5pad::kIntercepted)
		{
			buttons |= ps5pad::kIntercepted;
			any = true;
			continue;
		}
		// the d-pad's and the touchpad's bits mean nothing on a Sense controller
		buttons |= word & ~(ps5pad::kUp | ps5pad::kDown | ps5pad::kLeft | ps5pad::kRight | ps5pad::kTouchPad);
		const uint8_t x = state[kStickX], y = state[kStickY];
		if (y < 64)
			buttons |= ps5pad::kUp;
		else if (y > 192)
			buttons |= ps5pad::kDown;
		if (x < 64)
			buttons |= ps5pad::kLeft;
		else if (x > 192)
			buttons |= ps5pad::kRight;
		any = true;
	}
	return any;
}
