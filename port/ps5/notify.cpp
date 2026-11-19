// SPDX-License-Identifier: GPL-3.0-or-later
#include "notify.h"
#include "kernel.h"

#include <cstring>
#include <mutex>
#include <vector>

namespace
{
	std::mutex s_mutex;
	bool s_held = false;
	std::vector<std::string> s_heldMessages; // what came during the handoff, sent in order after it
	// the screen shows the window one notification at a time: the handoff's own are few, and more than
	// this is not worth keeping for a game that has started
	constexpr size_t kHeldMax = 4;

	void Post(const std::string& message)
	{
		SceNotificationRequest request{};
		std::strncpy(request.message, ("BotW VR: " + message).c_str(), sizeof(request.message) - 1);
		sceKernelSendNotificationRequest(0, &request, sizeof(request), 0);
	}
}

namespace ps5notify
{
	void Hold()
	{
		std::lock_guard lock(s_mutex);
		s_held = true;
		s_heldMessages.clear();
	}

	void Release()
	{
		std::vector<std::string> messages;
		{
			std::lock_guard lock(s_mutex);
			if (!s_held)
				return;
			s_held = false;
			messages.swap(s_heldMessages);
		}
		for (const std::string& message : messages)
			Post(message);
	}

	void Send(const std::string& message)
	{
		std::lock_guard lock(s_mutex);
		if (s_held)
		{
			if (s_heldMessages.size() < kHeldMax)
				s_heldMessages.push_back(message);
			return;
		}
		Post(message);
	}
}
