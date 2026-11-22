// SPDX-License-Identifier: GPL-3.0-or-later
#include "ime_dialog.h"

#ifndef PS5IME_CHECK
#include "../ps5/log.h"
#endif

#include <algorithm>
#include <cstring>
#include <mutex>

extern "C"
{
	// The console's user service (port/ps5/pad.cpp reads the same): the dialog belongs to a user.
	int32_t sceUserServiceGetForegroundUser(int32_t* userId);
	int32_t sceUserServiceGetInitialUser(int32_t* userId);
	// libSceImeDialog: the SDK's module stub, which tools/link.sh links with every other module
	int32_t sceImeDialogInit(const void* param, const void* extended);
	int32_t sceImeDialogGetStatus();
	int32_t sceImeDialogGetResult(void* result);
	int32_t sceImeDialogTerm();
	// libkernel's clock, for the dialog's own limit
	uint64_t sceKernelGetProcessTime();
}

namespace
{
	// The system reads this block: 96 bytes, as public PS5 homebrew declares it (see the header).
	struct ImeParam
	{
		int32_t userId;
		int32_t type;
		uint64_t supportedLanguages;
		int32_t enterLabel;
		int32_t inputMethod;
		void* filter;
		uint32_t option;
		uint32_t maxTextLength;
		uint16_t* inputTextBuffer;
		float posX;
		float posY;
		int32_t horizontalAlignment;
		int32_t verticalAlignment;
		const uint16_t* placeholder;
		const uint16_t* title;
		int8_t reserved[16];
	};
	static_assert(sizeof(ImeParam) == 96, "the IME dialog's parameter block is 96 bytes");
	struct ImeResult
	{
		int32_t outcome;
		int8_t reserved[12];
	};
	static_assert(sizeof(ImeResult) == 16, "the IME dialog's result is 16 bytes");

	constexpr int32_t kTypeDefault = 0; // the user's own keyboard, every language the console has
	constexpr int32_t kStatusNone = 0;
	constexpr int32_t kStatusRunning = 1;
	constexpr int32_t kStatusFinished = 2;
	constexpr int32_t kOutcomeOk = 0;
	constexpr int32_t kOutcomeCancelled = 1;
	constexpr int32_t kOutcomeAborted = 2;
	// A dialog that reports NONE once it was taken never comes up on this firmware: a second, then the
	// caller cancels the game's keyboard. The hook polls once a frame.
	constexpr int kGracePolls = 120;
	// A dialog the player cannot see (or one left unanswered) must not hold the game: after 90 s of
	// the console's clock it ends as a cancel would, and the game's own keyboard is cancelled with it.
	constexpr uint64_t kAnswerTimeUs = 90ull * 1000 * 1000;
	// No title and no placeholder of the app's own: the field holds the game's text, and the port has no
	// name for another game's prompt. Empty rather than null, so the system never reads an address it was
	// not given.
	const uint16_t kNothing[] = {0};

	std::mutex s_mutex; // Open, Poll and Close, which the stop watchdog also reaches
	ImeParam s_param;
	uint16_t s_text[ps5ime::kMaxTextLength + 1]; // the dialog writes here while it is up: it outlives the calls
	int s_polls;
	uint64_t s_openedAt; // sceKernelGetProcessTime, for the answer time
	bool s_open;
	bool s_terminated;
#ifndef PS5IME_CHECK
	// What this dialog says of itself, capped like the port's other diagnostics: the start with its
	// parameters and return code, each status it reports, and the result. Never the text.
	int s_logLines = 0;
	constexpr int kLogLines = 24;
	int32_t s_lastStatus = -1; // the status GetStatus last gave, so only a change is a line
#endif

	void CloseLocked()
	{
		if (!s_open)
			return;
		s_open = false;
		if (s_terminated)
			return;
		s_terminated = true;
#ifdef PS5IME_CHECK
		(void)sceImeDialogTerm();
#else
		const int32_t rc = sceImeDialogTerm();
		if (rc < 0)
			ps5log::Line("[ime] ending the console's keyboard: {:#x}", (uint32_t)rc);
#endif
	}
}

ps5ime::Keyboard ps5ime::KeyboardFor(bool stereo, bool menuOpen)
{
	if (menuOpen)
		return Keyboard::None;
	// Stereo is the console's dialog even while VrPS5's HeadsetUp() says 0: that flag belongs to the M2
	// flat path, and BetterVR's runtime starts Hmd2, so it is 0 in every stereo session (VrPS5.cpp).
	return stereo ? Keyboard::Console : Keyboard::Sheet;
}

bool ps5ime::Open(std::u16string_view text, int maxLength, int32_t& rc)
{
	std::lock_guard lock(s_mutex);
	CloseLocked();
	rc = 0;
	const uint32_t limit = (uint32_t)std::clamp(maxLength, 0, (int)kMaxTextLength);
	if (limit == 0)
		return false; // no room for a character: nothing to type
	int32_t user = -1;
	if (sceUserServiceGetForegroundUser(&user) < 0 || user < 0)
		sceUserServiceGetInitialUser(&user);
	std::memset(&s_param, 0, sizeof(s_param));
	s_param.userId = user >= 0 ? user : 0;
	s_param.type = kTypeDefault;
	s_param.maxTextLength = limit;
	s_param.inputTextBuffer = s_text;
	s_param.placeholder = kNothing;
	s_param.title = kNothing;
	const size_t copy = std::min<size_t>(text.size(), limit);
	for (size_t i = 0; i < copy; i++)
		s_text[i] = text[i];
	s_text[copy] = 0;
	s_polls = 0;
	s_terminated = false;
#ifndef PS5IME_CHECK
	s_lastStatus = -1;
#endif
	rc = sceImeDialogInit(&s_param, nullptr);
#ifndef PS5IME_CHECK
	if (s_logLines < kLogLines)
	{
		++s_logLines;
		ps5log::Line("[ime] init: type {}, limit {}, user {}; rc {:#x}", s_param.type, limit, s_param.userId, (uint32_t)rc);
	}
#endif
	if (rc < 0)
		return false;
	s_openedAt = sceKernelGetProcessTime();
	s_open = true;
	return true;
}

int ps5ime::Poll(std::u16string& text, int32_t& rc)
{
	std::lock_guard lock(s_mutex);
	rc = 0;
	if (!s_open)
	{
		rc = -1;
		return -1;
	}
	const int32_t status = sceImeDialogGetStatus();
#ifndef PS5IME_CHECK
	if (status != s_lastStatus)
	{
		s_lastStatus = status;
		if (s_logLines < kLogLines)
		{
			++s_logLines;
			ps5log::Line("[ime] status: {:#x}", (uint32_t)status);
		}
	}
#endif
	if (status == kStatusRunning)
	{
		s_polls = 0;
		if (sceKernelGetProcessTime() - s_openedAt >= kAnswerTimeUs)
		{
			CloseLocked();
			return 4; // up too long: a cancel for the game's keyboard
		}
		return 0;
	}
	if (status == kStatusNone)
	{
		if (++s_polls < kGracePolls)
			return 0;
		CloseLocked();
		rc = -1;
		return -1; // taken, never shown
	}
	if (status != kStatusFinished)
	{
		CloseLocked();
		rc = status;
		return -1;
	}
	ImeResult result{};
	rc = sceImeDialogGetResult(&result);
#ifndef PS5IME_CHECK
	if (s_logLines < kLogLines)
	{
		++s_logLines;
		if (rc < 0)
			ps5log::Line("[ime] result: rc {:#x}", (uint32_t)rc);
		else
			ps5log::Line("[ime] result: outcome {:#x}, rc {:#x}", (uint32_t)result.outcome, (uint32_t)rc);
	}
#endif
	if (rc < 0)
	{
		CloseLocked();
		return -1;
	}
	const int32_t outcome = result.outcome;
	// the buffer's text is taken before the dialog is ended, whatever Term makes of it
	std::u16string typed;
	if (outcome == kOutcomeOk)
	{
		const size_t limit = std::min<size_t>(s_param.maxTextLength, kMaxTextLength);
		size_t length = 0;
		while (length < limit && s_text[length])
			length++;
		typed.assign((const char16_t*)s_text, length);
	}
	CloseLocked();
	rc = outcome;
	if (outcome == kOutcomeOk)
	{
		text = typed;
		return 1;
	}
	if (outcome == kOutcomeCancelled)
		return 2;
	if (outcome != kOutcomeAborted)
		rc = outcome; // an ending this build does not know: a cancel for the game's keyboard
	return 3;
}

void ps5ime::Close()
{
	std::lock_guard lock(s_mutex);
	CloseLocked();
}
