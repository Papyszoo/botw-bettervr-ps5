// SPDX-License-Identifier: GPL-3.0-or-later
// The console's keyboard dialog (port/app/ime_dialog.cpp) on a PC: the system's calls are a fake, and
// what is checked is the state around them - the game's text and limit, the NONE grace, OK, cancel,
// abort, a dialog left up past its 90 s, a close once, a fresh dialog after one ended, which keyboard a
// stereo or a flat frame gets, and the module (sysmodule 0x96) and dialogs that must be there before the
// first call into them. tools/tests/test-ime-dialog.sh builds and runs it. Each check prints PASS or
// FAIL; the exit status is the failures.

#include "app/ime_dialog.h"

#include <cstdint>
#include <cstdio>
#include <string>

namespace
{
	int failures = 0;

	void Check(bool ok, const char* what)
	{
		std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
		failures += !ok;
	}

	constexpr uint64_t kSecond = 1'000'000; // sceKernelGetProcessTime's microseconds

	// The system's parameter block, read by the fake as the dialog hands it over.
	struct Param
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
	static_assert(sizeof(Param) == 96, "the IME dialog's parameter block is 96 bytes");

	uint64_t g_now = 1'000'000;
	int32_t g_initResult = 0;
	int32_t g_status = 0;
	int32_t g_resultCode = 0;
	int32_t g_outcome = 0;
	int g_inits = 0, g_terms = 0;
	int32_t g_userId = 0;
	uint16_t* g_buffer = nullptr;
	uint32_t g_limit = 0;
	bool g_foreground = true;
	// the console's keyboard module (sysmodule 0x96) and its dialogs
	int32_t g_moduleResult = 0;
	int32_t g_dialogResult = 0;
	uint16_t g_moduleId = 0;
	int g_moduleLoads = 0, g_dialogInits = 0;

	void Reset()
	{
		ps5ime::Close(); // whatever the last case left open
		ps5ime::ForgetModule(); // a new process asks the system for the module and its dialogs again
		g_now = 1'000'000;
		g_initResult = 0;
		g_status = 0;
		g_resultCode = 0;
		g_outcome = 0;
		g_inits = 0;
		g_terms = 0;
		g_userId = 0;
		g_buffer = nullptr;
		g_limit = 0;
		g_foreground = true;
		g_moduleResult = 0;
		g_dialogResult = 0;
		g_moduleId = 0;
		g_moduleLoads = 0;
		g_dialogInits = 0;
	}

	bool Open(const char16_t* text, int limit, int32_t& rc)
	{
		return ps5ime::Open(std::u16string_view(text), limit, rc);
	}

	// what the player typed in the system's field
	void Type(const char16_t* text)
	{
		size_t i = 0;
		for (; text[i]; i++)
			g_buffer[i] = text[i];
		g_buffer[i] = 0;
	}

	std::u16string Field()
	{
		return std::u16string(reinterpret_cast<const char16_t*>(g_buffer));
	}
}

extern "C"
{
	int32_t sceUserServiceGetForegroundUser(int32_t* userId)
	{
		if (!g_foreground)
			return -1;
		*userId = 7;
		return 0;
	}

	int32_t sceUserServiceGetInitialUser(int32_t* userId)
	{
		*userId = 9;
		return 0;
	}

	int32_t sceSysmoduleLoadModule(uint16_t moduleId)
	{
		++g_moduleLoads;
		g_moduleId = moduleId;
		return g_moduleResult;
	}

	int32_t sceCommonDialogInitialize()
	{
		++g_dialogInits;
		return g_dialogResult;
	}

	int32_t sceImeDialogInit(const void* param, const void*)
	{
		++g_inits;
		if (g_initResult < 0)
			return g_initResult;
		const Param* field = static_cast<const Param*>(param);
		g_userId = field->userId;
		g_limit = field->maxTextLength;
		g_buffer = field->inputTextBuffer;
		return g_initResult;
	}

	int32_t sceImeDialogGetStatus()
	{
		return g_status;
	}

	int32_t sceImeDialogGetResult(void* result)
	{
		if (g_resultCode >= 0)
			*static_cast<int32_t*>(result) = g_outcome;
		return g_resultCode;
	}

	int32_t sceImeDialogTerm()
	{
		++g_terms;
		return 0;
	}

	uint64_t sceKernelGetProcessTime()
	{
		return g_now;
	}
}

int main()
{
	int32_t rc = 0;
	std::u16string text;

	// a new appearance hands the game's text and limit to the system, after the module is loaded and the
	// dialogs started
	Reset();
	Check(Open(u"Link", 10, rc), "a dialog opens");
	Check(g_inits == 1 && g_limit == 10 && g_userId == 7, "the game's limit and the foreground user reach the system");
	Check(g_moduleLoads == 1 && g_moduleId == 0x0096 && g_dialogInits == 1,
		"the keyboard's module (0x96) is loaded and the dialogs started once, before the first dialog");
	Check(g_buffer != nullptr && Field() == u"Link", "the game's text is in the system's field");

	// a taken dialog that has not appeared yet is given its second, then closed once
	Reset();
	Check(Open(u"x", 8, rc), "a dialog opens for the no-status grace");
	g_status = 0;
	int state = 0;
	for (int i = 0; i < 119 && state == 0; i++)
		state = ps5ime::Poll(text, rc);
	Check(state == 0 && g_terms == 0, "a dialog that has not appeared is waited out");
	state = ps5ime::Poll(text, rc);
	Check(state == -1 && g_terms == 1, "a dialog that never appears is closed after its grace");
	Check(ps5ime::Poll(text, rc) == -1 && g_terms == 1, "a closed dialog has nothing left to poll");

	// a dialog that stays up without an answer ends itself after 90 s, as a cancel would
	Reset();
	Check(Open(u"x", 8, rc), "a dialog opens for the time limit");
	g_status = 1;
	g_now += 89 * kSecond;
	Check(ps5ime::Poll(text, rc) == 0 && g_terms == 0, "a dialog being typed on is left up before 90 s");
	g_now += 2 * kSecond;
	Check(ps5ime::Poll(text, rc) == 4 && g_terms == 1, "a dialog left unanswered past 90 s ends itself, once");

	// OK: the typed text is the game's, and the dialog is ended once
	Reset();
	Check(Open(u"abc", 16, rc), "a dialog opens for OK");
	g_status = 2;
	g_outcome = 0;
	Type(u"Hello");
	text = u"unchanged";
	Check(ps5ime::Poll(text, rc) == 1 && text == u"Hello" && g_terms == 1, "OK gives the typed text and ends the dialog once");

	// cancel, abort and an ending this build does not know: the game's own text stands
	Reset();
	Check(Open(u"abc", 16, rc), "a dialog opens for a cancel");
	g_status = 2;
	g_outcome = 1;
	text = u"keep";
	Check(ps5ime::Poll(text, rc) == 2 && text == u"keep" && rc == 1 && g_terms == 1, "a cancel leaves the game's text and ends the dialog");
	Reset();
	Check(Open(u"abc", 16, rc), "a dialog opens for an abort");
	g_status = 2;
	g_outcome = 2;
	Check(ps5ime::Poll(text, rc) == 3 && rc == 2 && g_terms == 1, "an aborted dialog is reported and closed");
	Reset();
	Check(Open(u"abc", 16, rc), "a dialog opens for an unknown ending");
	g_status = 2;
	g_outcome = 7;
	Check(ps5ime::Poll(text, rc) == 3 && rc == 7 && g_terms == 1, "an unknown ending is reported with its code and closed");

	// a result that cannot be read, and a status this build does not know
	Reset();
	Check(Open(u"abc", 16, rc), "a dialog opens for a failed result");
	g_status = 2;
	g_resultCode = -5;
	Check(ps5ime::Poll(text, rc) == -1 && rc == -5 && g_terms == 1, "a result that cannot be read closes the dialog");
	Reset();
	Check(Open(u"abc", 16, rc), "a dialog opens for an unknown status");
	g_status = 9;
	Check(ps5ime::Poll(text, rc) == -1 && rc == 9 && g_terms == 1, "an unknown status is reported and closed");

	// no room for a character, and a game's larger limit clamped to the dialog's
	Reset();
	Check(!Open(u"x", 0, rc) && rc == 0 && g_inits == 0, "no room for a character opens nothing");
	Check(!Open(u"x", -3, rc) && g_inits == 0, "a negative limit opens nothing");
	Check(Open(u"x", 5000, rc) && g_limit == 2048, "a game's larger limit is clamped to the dialog's 2048");

	// the game's text is cut to its limit, as the field holds it
	Reset();
	Check(Open(u"abcdef", 3, rc) && g_limit == 3 && Field() == u"abc", "the game's text is cut to its limit");

	// a later appearance opens a fresh dialog, and an open on top of one ends the first
	Reset();
	Check(Open(u"one", 8, rc), "a dialog opens before a second appearance");
	g_status = 2;
	g_outcome = 0;
	Type(u"two");
	Check(ps5ime::Poll(text, rc) == 1 && text == u"two", "the first dialog answers");
	Check(Open(u"three", 8, rc) && g_inits == 2 && Field() == u"three", "a later appearance opens a fresh dialog");
	Reset();
	Check(Open(u"one", 8, rc), "a dialog opens to be replaced");
	Check(Open(u"two", 8, rc) && g_inits == 2 && g_terms == 1 && Field() == u"two", "a new open ends the old dialog first");

	// a refused dialog is no dialog; a later try can open one, without asking for the module again
	Reset();
	g_initResult = -12345;
	Check(!Open(u"x", 8, rc) && rc == -12345 && g_inits == 1 && g_terms == 0, "a refused dialog opens nothing and ends nothing");
	g_initResult = 0;
	Check(Open(u"x", 8, rc) && g_inits == 2, "a later try can open the dialog");
	Check(g_moduleLoads == 1 && g_dialogInits == 1, "a second dialog loads no module and starts no dialogs again");

	// The console's keyboard is a system module (0x0096) and its dialogs are started before the first
	// call: a system without either is declined, never called through a null import (the console run of
	// 2026-11-22 crashed inside the first sceImeDialogInit for exactly that).
	Reset();
	g_moduleResult = -1;
	Check(!Open(u"x", 8, rc) && rc == -1 && g_inits == 0 && g_dialogInits == 0,
		"a keyboard module that does not load opens nothing and starts no dialogs");
	Check(g_moduleLoads == 1 && g_moduleId == 0x0096, "the module asked for is 0x96, once");
	g_moduleResult = 0;
	Check(!Open(u"x", 8, rc) && g_moduleLoads == 1 && g_inits == 0, "a module that did not load is not asked for again");
	Reset();
	g_dialogResult = (int32_t)0x80b80003;
	Check(!Open(u"x", 8, rc) && rc == (int32_t)0x80b80003 && g_inits == 0, "dialogs that do not start open nothing");
	Check(g_moduleLoads == 1 && g_dialogInits == 1, "the module loads and the dialogs start once, after it");
	g_dialogResult = 0;
	Check(!Open(u"x", 8, rc) && g_dialogInits == 1 && g_inits == 0, "dialogs that did not start are not started again");
	Reset();
	g_dialogResult = (int32_t)0x80b80002;
	Check(Open(u"x", 8, rc) && g_inits == 1, "dialogs already started (0x80b80002) are no failure");

	// Close ends an open dialog exactly once, and nothing else
	Reset();
	Check(Open(u"x", 8, rc), "a dialog opens for Close");
	ps5ime::Close();
	Check(g_terms == 1, "Close ends an open dialog once");
	ps5ime::Close();
	Check(g_terms == 1, "Close on a closed dialog ends nothing");
	Reset();
	Check(ps5ime::Poll(text, rc) == -1 && rc == -1, "nothing is polled with no dialog");

	// without a foreground user the initial user's dialog still opens
	Reset();
	g_foreground = false;
	Check(Open(u"x", 8, rc) && g_userId == 9, "a dialog opens on the initial user without a foreground one");

	// The keyboard each hook opens: stereo is the console's dialog whatever VrPS5's own frame path calls
	// its headset (the console run of 2026-11-22 was stereo 1, headset 0, and the dialog must open), flat
	// is the sheet, and under the port's menu neither is drawn.
	Check(ps5ime::KeyboardFor(true, false) == ps5ime::Keyboard::Console, "stereo opens the console's keyboard, though the headset flag of that run was 0");
	Check(ps5ime::KeyboardFor(false, false) == ps5ime::Keyboard::Sheet, "flat play keeps the port's sheet");
	Check(ps5ime::KeyboardFor(true, true) == ps5ime::Keyboard::None, "under the menu the console's keyboard does not open");
	Check(ps5ime::KeyboardFor(false, true) == ps5ime::Keyboard::None, "under the menu the sheet does not open");

	std::printf("%s (%d failed)\n", failures ? "FAILED" : "all passed", failures);
	return failures;
}
