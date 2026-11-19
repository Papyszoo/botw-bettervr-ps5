// SPDX-License-Identifier: GPL-3.0-or-later
// PS5Cemu: the system's notification toast (top right of the screen).

#pragma once

#include <string>

namespace ps5notify
{
	// The PS5 shows the notification window, with the console's earlier ones behind it, over the
	// screen: nothing of ours is drawn in the handoff from the launcher to a game, so what the app
	// sends then waits for the game's own VideoOut surface rather than bringing that window up on a
	// black screen. Hold at the launcher giving the screen up, Release once the surface is there.
	void Hold();
	void Release();
	void Send(const std::string& message);
}
