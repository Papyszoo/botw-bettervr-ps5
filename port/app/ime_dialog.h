// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: the console's own on-screen keyboard (libSceImeDialog), for the one place the port's
// keyboard cannot be seen. BetterVR's stereo frames draw Cemu's overlays into a TV picture neither the
// headset nor the TV shows, so a Wii U game's software keyboard (nn::swkbd) is a keyboard nobody can
// see or type on (app/ingame.cpp's PS5Cemu_DrawKeyboard, patches/cemu 0039). While stereo runs, Cemu's
// swkbd state drives this dialog instead, from outside Cemu's ImGui frame (PS5Cemu_DrawKeyboardVR,
// patches/cemu 0066): the game's text and length limit go in, the typed text comes back and is decided
// as the port's sheet's was, and a cancel (or a dialog that cannot open) ends the game's own keyboard as
// a cancel on the hardware does, so the game never waits on a keyboard nobody can see.
//
// Which hook opens this dialog is KeyboardFor below: stereo, whatever VrPS5's own frame path reports.
// That flag is not "a headset is worn": it is true only while VrPS5's M2 flat-screen path is copying
// frames, and BetterVR's own OpenXR runtime starts Hmd2 instead, so the flag is false in stereo.
//
// The parameter block, the statuses and the outcomes are the ones public PS5 homebrew uses for this
// dialog (offact's IME_dialog.c and prospero-win's pw_osk_ps5.c, GPL and LGPL homebrew), whose 96-byte
// block and 16-byte result the PS4 reimplementations share; its text is 16 bits a unit whatever the
// toolchain's wchar_t is. There is no SDK header for it and none is used here. The keyboard is sysmodule
// 0x0096, which a title does not start with: Open loads it once and starts the dialogs
// (libSceCommonDialog) before the first call, and declines as a cancel until both answer, so a missing
// module is never a null call (the console run of 2026-11-22 crashed at one). The SDK's libSceImeDialog
// stub is linked; for libSceCommonDialog tools/link.sh builds a link-only one, and tools/package.sh
// names it to the converter.
//
// Open and Poll run on Cemu's render thread, the one its TV frame runs on, where the game's form is
// read as it was for the port's sheet. Close also comes from the stop watchdog (app/emulator.cpp's
// LeaveEndingToSystem) before the process is replaced; one mutex keeps the three apart, so a Term never
// runs inside a poll.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace ps5ime
{
	// The dialog's longest text, in UTF-16 units: a game's larger limit is clamped to it.
	constexpr int kMaxTextLength = 2048;
	// Which keyboard a game appearance gets, from where the player is looking. Stereo is the console's
	// dialog, whatever VrPS5's own frame path says about a headset: in stereo Cemu's own picture (where
	// the port's sheet draws) is neither eye's picture, and the TV shows Hmd2's social screen. Flat play,
	// the M2 flat screen and VR off are the sheet; under the port's menu, neither is drawn.
	enum class Keyboard
	{
		None,
		Console,
		Sheet
	};
	Keyboard KeyboardFor(bool stereo, bool menuOpen);
	// Opens the dialog with the game's text and its limit in UTF-16 units. True when the system took
	// it; false with rc set (or rc 0 when there is no room for a character) when it did not, and the
	// caller ends the game's keyboard as cancelled so it never waits on a keyboard that cannot show.
	bool Open(std::u16string_view text, int maxLength, int32_t& rc);
	// One poll, once a frame while Up: 0 while the player types, 1 when OK (text is what was typed),
	// 2 when the player cancelled, 3 when the system abandoned it, 4 when no answer came within 90 s;
	// negative, with rc, when it never appeared or could not be read. Every ending closes it; 2, 3, 4
	// and negative are a cancel for the game's keyboard.
	int Poll(std::u16string& text, int32_t& rc);
	// Ends it whatever its state, so the next appearance opens a fresh one. Called when the app leaves
	// the game or VR stops under it.
	void Close();
#ifdef PS5IME_CHECK
	// The host test's fake system: forget the module's load, its import slots and the dialogs' start,
	// which happen once per process, so each case starts as a new one.
	void ForgetModule();
#endif
}
