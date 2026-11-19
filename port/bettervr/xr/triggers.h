// SPDX-License-Identifier: GPL-3.0-or-later
// BotW-BetterVR on PS5: the adaptive triggers' payload, as libScePad's scePadSetTriggerEffect takes it.
//
// The 0x78-byte ScePadTriggerEffectParam and its two 0x38-byte ScePadTriggerEffectCommands are the DualSense's
// layout (the PS5 SDK's header, as Steamworks ships it): a mask at +0x00 (bit 0 L2, bit 1 R2), the L2 command at
// +0x08 and the R2 command at +0x40, each a u32 mode, four padding bytes and 0x30 bytes of data. Modes are 0 off,
// 1 feedback {position, strength}, 2 weapon {start, end, strength}, 3 vibration {position, amplitude, frequency},
// 4 multi-position feedback, 5 slope {start, end, startStrength, endStrength}, 6 multi-position vibration;
// positions are 0-9, strengths 0-8.
//
// The same command carries the Sense controllers' experiment (runtime.cpp): the VR payload is unpublished.
#ifndef PS5_BETTERVR_TRIGGERS_H
#define PS5_BETTERVR_TRIGGERS_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace ps5trig
{
	constexpr uint8_t kMaskL2 = 1, kMaskR2 = 2, kMaskBoth = kMaskL2 | kMaskR2;

	enum Mode : uint32_t
	{
		kOff = 0,
		kFeedback = 1,
		kWeapon = 2,
		kVibration = 3,
		kMultiFeedback = 4,
		kSlope = 5,
		kMultiVibration = 6,
	};

	struct Command
	{
		uint32_t mode = kOff;
		uint8_t reserved[4] = {};
		uint8_t data[0x30] = {};
	};
	static_assert(sizeof(Command) == 0x38, "ScePadTriggerEffectCommand is 0x38 bytes");

	struct Param
	{
		uint8_t mask = kMaskBoth;
		uint8_t rumbleEmulation = 0;
		uint8_t reserved[6] = {};
		Command l2;
		Command r2;
	};
	static_assert(sizeof(Command) == 0x38, "ScePadTriggerEffectCommand is 0x38 bytes");
	static_assert(offsetof(Command, mode) == 0x00 && offsetof(Command, data) == 0x08, "a command's mode is at 0x00 and its data at 0x08");
	static_assert(sizeof(Param) == 0x78, "ScePadTriggerEffectParam is 0x78 bytes");
	static_assert(offsetof(Param, mask) == 0x00, "the trigger mask is at 0x00");
	static_assert(offsetof(Param, l2) == 0x08 && offsetof(Param, r2) == 0x40, "the commands sit at 0x08 and 0x40");

	inline Command Off()
	{
		return Command{};
	}

	// A firm stop at one position: "a stiff spring that starts at Position", for the shield.
	inline Command Feedback(uint8_t position, uint8_t strength)
	{
		Command command;
		command.mode = kFeedback;
		command.data[0] = position;
		command.data[1] = strength;
		return command;
	}

	// The bow, from the game's live draw (0-1, 1.5 is its own power-shot clamp). Slope mode ramps from 1 at the
	// released position to a strength that grows with the draw at the fully pulled position, and stays there: the
	// trigger gets firmer as the bow is drawn. Below 2 % draw, and at strength 0, it is off.
	inline Command Bow(float draw, float strength)
	{
		const float amount = std::clamp(draw, 0.0f, 1.0f);
		const float scale = std::clamp(strength, 0.0f, 1.0f);
		if (amount < 0.02f || scale <= 0.0f)
			return Off();
		Command command;
		command.mode = kSlope;
		command.data[0] = 0; // start position
		command.data[1] = 9; // end position
		command.data[2] = 1; // start strength
		command.data[3] = (uint8_t)(1 + std::lround(7.0f * amount * scale)); // 1-8: the end strength
		return command;
	}

	// The shield's click: position 2, strength 3-5 across the setting, as the rumble research found it. Off at 0.
	inline Command Shield(float strength)
	{
		const float scale = std::clamp(strength, 0.0f, 1.0f);
		if (scale <= 0.0f)
			return Off();
		return Feedback(2, (uint8_t)(3 + std::lround(2.0f * scale)));
	}

	// Both commands in one parameter: the mask says both are updated, so an off pair clears each trigger.
	inline Param Both(const Command& l2, const Command& r2)
	{
		Param param;
		param.mask = kMaskBoth;
		param.l2 = l2;
		param.r2 = r2;
		return param;
	}

	inline bool IsOff(const Param& param)
	{
		return param.l2.mode == kOff && param.r2.mode == kOff;
	}

	inline bool Same(const Param& a, const Param& b)
	{
		return std::memcmp(&a, &b, sizeof a) == 0;
	}
} // namespace ps5trig

#endif
