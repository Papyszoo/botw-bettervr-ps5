// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: the console's own on-screen keyboard (libSceImeDialog), for the one place the port's
// keyboard cannot be seen. BetterVR's stereo frames draw Cemu's overlays into a TV picture neither the
// headset nor the TV shows, so a Wii U game's software keyboard (nn::swkbd) is a keyboard nobody can
// see or type on (app/ingame.cpp's PS5Cemu_DrawKeyboard, patches/cemu 0039). While stereo runs, that
// hook opens this dialog instead: the game's text and length limit go in, the typed text comes back
// and is decided as the port's sheet's was, and a cancel decides the text the game gave, because
// Cemu's swkbd has no cancel to take.
//
// The parameter block, the statuses and the outcomes are the ones public PS5 homebrew uses for this
// dialog (offact's IME_dialog.c and prospero-win's pw_osk_ps5.c, GPL and LGPL homebrew), whose 96-byte
// block and 16-byte result the PS4 reimplementations share; its text is 16 bits a unit whatever the
// toolchain's wchar_t is. There is no SDK header for it and none is used here: the SDK ships the
// module's stub, which tools/link.sh already links.
//
// Everything here runs on Cemu's render thread, the one swkbd_render runs on: swkbd's state stays on
// that thread, as it does for the port's sheet.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace ps5ime
{
	// Opens the dialog with the game's text and its limit in UTF-16 units. True when the system took
	// it; false with rc set (or rc 0 when there is no room for a character) when it did not, and the
	// caller decides the text it has so the game never waits on a keyboard that cannot show.
	bool Open(std::u16string_view text, int maxLength, int32_t& rc);
	// One poll, once a frame while Up: 0 while the player types, 1 when OK (text is what was typed),
	// 2 when the player cancelled, 3 when the system abandoned it; negative, with rc, when it never
	// appeared or could not be read. Every ending closes it.
	int Poll(std::u16string& text, int32_t& rc);
	// Ends it whatever its state, so the next appearance opens a fresh one. Called when the app leaves
	// the game or VR stops under it.
	void Close();
}
