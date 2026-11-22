// SPDX-License-Identifier: MPL-2.0
// PS5Cemu: Cemu's audio output on the PS5's AudioOut.
//
// Cemu hands over blocks of 16-bit samples (stereo, or 5.1 for the TV if configured). A worker
// thread feeds AudioOut 256-frame stereo grains at 48 kHz; sceAudioOutOutput blocks until the
// previous grain is playing, which paces the thread. A grain is played only once a whole one is
// queued: a short one padded with silence is a splice, heard as a click every time the stream's
// clock and the console's cross (the 2 Hz click of v0.6). Surround streams are mixed down to
// stereo, the format every PS5 audio output accepts. The GamePad's sound goes to player 1's
// DualSense speaker instead (its own port, one channel).

#include "PS5AudioAPI.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <string>
#include <string_view>

#include "util/helpers/helpers.h"
#include "../ps5/pad.h"
#include "PS5AxFeed.h"	 // port/cemu: the AX output's feed gap, for the gap line here
#include "PS5CoreDiag.h" // port/cemu: what each emulated core is doing, for the same line

void PS5Cemu_LogLine(std::string_view line); // the port's boot log (port/ps5/log.cpp)

extern "C"
{
	int sceAudioOutInit(void);
	int sceAudioOutOpen(int userId, int type, int index, uint32_t length, uint32_t frequency, uint32_t format);
	int sceAudioOutClose(int handle);
	int sceAudioOutOutput(int handle, const void* samples);
	int sceAudioOutSetVolume(int handle, int flags, const int* volumes);
	void* scePthreadSelf();
	int scePthreadGetprio(void* thread, int* priority);
	int scePthreadSetprio(void* thread, int priority);
}

namespace
{
	constexpr int kUserSystem = 0xff;	 // the port belongs to the system, not to one user
	constexpr int kPortMain = 0;
	constexpr int kPortPadSpeaker = 4;	 // a controller's speaker, its user's
	constexpr uint32 kGrain = 256;		 // frames per sceAudioOutOutput
	constexpr uint32 kRate = 48000;
	constexpr uint32 kFormatStereoS16 = 1, kFormatMonoS16 = 0;
	constexpr int kVolumeFlagsLeftRight = 3;
	constexpr int kVolume0dB = 32768;
	// Cemu's blocks that wait for AudioOut: 4 blocks are 48 ms, nine grains, so a hitch in the game's
	// audio thread never leaves a grain short. The old 2 blocks (24 ms) sat in the grain and block
	// jitter itself, where the fast refill kept crossing it.
	constexpr uint32 kTargetBlocks = 4;
	constexpr uint32 kMaxTargetBlocks = 12; // the audio_delay setting's ceiling here: 144 ms
	constexpr uint32 kSpareBlocks = 8;	   // room for the drift and a burst before the oldest block gives way
	// as the other ports' sound threads: above the emulation's, so a busy CPU does not leave
	// AudioOut without its next grain (256 the highest, 767 the lowest, threads start at 700)
	constexpr int kAudioPriority = 384;
	// a producer this late is not coming back for this grain (Cemu's own audio timeout is 216 ms):
	// silence plays and is counted, rather than the thread waiting forever
	constexpr int kPadTimeoutMs = 250;
}

bool PS5AudioAPI::InitializeStatic()
{
	// The launcher may have initialised AudioOut already, which this call then reports as an
	// error; whether a port really opens shows in the constructor.
	static const int result = sceAudioOutInit();
	(void)result;
	return true;
}

std::vector<IAudioAPI::DeviceDescriptionPtr> PS5AudioAPI::GetDevices()
{
	return {std::make_shared<PS5DeviceDescription>(L"PS5 audio output", kDeviceId),
		std::make_shared<PS5DeviceDescription>(L"DualSense speaker", kPadSpeakerId)};
}

PS5AudioAPI::PS5AudioAPI(bool padSpeaker, uint32 samplerate, uint32 channels, uint32 samples_per_block, uint32 bits_per_sample)
	: IAudioAPI(samplerate, channels, samples_per_block, bits_per_sample), m_padSpeaker(padSpeaker)
{
	if (bits_per_sample != 16 || samplerate != kRate)
		throw std::runtime_error(fmt::format("PS5 AudioOut: unsupported stream ({} Hz, {} bit)", samplerate, bits_per_sample));
	if (padSpeaker)
	{
		const int32_t user = ps5pad::UserId(0);
		m_port = user >= 0 ? sceAudioOutOpen(user, kPortPadSpeaker, 0, kGrain, kRate, kFormatMonoS16) : user;
		cemuLog_log(LogType::Force, "PS5 AudioOut: the GamePad's sound on the DualSense speaker of user {:#x}: {}", (uint32)user,
			m_port >= 0 ? "open" : fmt::format("{:#x}", (uint32)m_port));
	}
	else
		m_port = sceAudioOutOpen(kUserSystem, kPortMain, 0, kGrain, kRate, kFormatStereoS16);
	if (m_port < 0)
		throw std::runtime_error(fmt::format("PS5 AudioOut: sceAudioOutOpen failed ({:#x})", (uint32)m_port));
	const std::array<int, 8> volumes{kVolume0dB, kVolume0dB, kVolume0dB, kVolume0dB, kVolume0dB, kVolume0dB, kVolume0dB, kVolume0dB};
	sceAudioOutSetVolume(m_port, kVolumeFlagsLeftRight, volumes.data());
	// Cemu's own target, but never less than four blocks: the output takes 256-frame grains, and a
	// target inside the block and grain jitter is what left grains short (see PS5AudioQueue.h)
	const uint32 target = std::clamp<uint32>(GetAudioDelay(), kTargetBlocks, kMaxTargetBlocks) * samples_per_block;
	m_queue = PS5AudioQueue(channels, samples_per_block, kGrain, target, target + kSpareBlocks * samples_per_block);
	m_lastCountLog = std::chrono::steady_clock::now(); // the first diagnostics line 10 s in
	m_thread = std::thread(&PS5AudioAPI::OutputThread, this);
}

PS5AudioAPI::~PS5AudioAPI()
{
	{
		std::lock_guard lock(m_mutex);
		m_quit = true;
	}
	m_wake.notify_all();
	if (m_thread.joinable())
		m_thread.join();
	if (m_port >= 0)
	{
		sceAudioOutOutput(m_port, nullptr); // wait for the last grain
		sceAudioOutClose(m_port);
	}
}

bool PS5AudioAPI::NeedAdditionalBlocks() const
{
	std::lock_guard lock(m_mutex);
	return m_queue.NeedsMore();
}

bool PS5AudioAPI::FeedBlock(sint16* data)
{
	{
		std::lock_guard lock(m_mutex);
		m_queue.Push(data);
	}
	// the output thread may be waiting for a whole grain
	m_wake.notify_all();
	return true;
}

bool PS5AudioAPI::Play()
{
	std::lock_guard lock(m_mutex);
	m_playing = true;
	m_wake.notify_all();
	return true;
}

bool PS5AudioAPI::Stop()
{
	std::lock_guard lock(m_mutex);
	m_playing = false;
	m_queue.Clear(); // the pause's sound is not the sound of where play goes on
	// the diagnostics start over: a pause empties the queue by design, and that is not a drain
	m_lowestFrames = 0xFFFFFFFF;
	m_longestGrainWaitNs = 0;
	m_settled = false;
	m_lastAxGapNs = 0;
	return true;
}

void PS5AudioAPI::OutputThread()
{
	SetThreadName("PS5AudioOut");
	RaisePriority();
	std::array<sint16, kGrain * 2> grain{};
	const uint32 channels = m_channels;
	for (;;)
	{
		{
			std::unique_lock lock(m_mutex);
			m_wake.wait(lock, [this] { return m_quit || m_playing; });
			if (m_quit)
				return;
			if (!m_queue.HasGrain())
			{
				// a grain short by a few frames would be padded with silence: a splice, heard as a
				// click every time the stream's clock and AudioOut's cross. Wait for a whole one: the
				// four blocks of target make that a hitch, not a dropout, in steady play.
				// The wait is timed and the fill kept (LogCountsLocked): a drain the producer recovers
				// from is otherwise silent here, and it is what a warp's long frame leaves
				if (m_settled)
					m_lowestFrames = std::min<uint32>(m_lowestFrames, (uint32)m_queue.Frames());
				const auto waitStart = std::chrono::steady_clock::now();
				const bool got = m_wake.wait_for(lock, std::chrono::milliseconds(kPadTimeoutMs),
					[this] { return m_quit || !m_playing || m_queue.HasGrain(); });
				const uint64 waitedNs = (uint64)std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - waitStart).count();
				m_longestGrainWaitNs = std::max<uint64>(m_longestGrainWaitNs, waitedNs);
				if (!got)
					m_queue.CountPaddedGrain();
				if (m_quit)
					return;
				if (!m_playing)
					continue;
				if (m_settled)
					m_lowestFrames = std::min<uint32>(m_lowestFrames, (uint32)m_queue.Frames());
			}
			const float volume = std::clamp(m_volume, 0, 100) / 100.0f;
			const size_t available = m_queue.Frames();
			if (!m_queue.NeedsMore())
				m_settled = true; // the start's fill is not a drain: the line starts once it has reached its target
			if (m_settled)
				m_lowestFrames = std::min<uint32>(m_lowestFrames, (uint32)available);
			const size_t frames = std::min<size_t>(available, kGrain);
			for (size_t frame = 0; frame < frames; frame++)
			{
				const sint16* in = &m_queue.Data()[frame * channels];
				float left, right;
				if (channels == 1)
					left = right = in[0];
				else if (channels == 2)
				{
					left = in[0];
					right = in[1];
				}
				else
				{
					// FL FR FC LFE then the surround pairs: centre and surrounds at -3 dB
					float surroundLeft = 0, surroundRight = 0;
					for (uint32 c = 4; c + 1 < channels; c += 2)
					{
						surroundLeft += in[c];
						surroundRight += in[c + 1];
					}
					left = in[0] + 0.707f * (in[2] + surroundLeft);
					right = in[1] + 0.707f * (in[2] + surroundRight);
				}
				if (m_padSpeaker)
					grain[frame] = (sint16)std::clamp((left + right) * 0.5f * volume, -32768.0f, 32767.0f); // one channel
				else
				{
					grain[frame * 2] = (sint16)std::clamp(left * volume, -32768.0f, 32767.0f);
					grain[frame * 2 + 1] = (sint16)std::clamp(right * volume, -32768.0f, 32767.0f);
				}
			}
			std::fill(grain.begin() + frames * (m_padSpeaker ? 1 : 2), grain.end(), (sint16)0);
			m_queue.Consume(frames);
			LogCountsLocked();
			// once a grain (~5.3 ms): a hole in the AX output, sampled from this host thread's own
			// state under the mutex (only atomics are read from the emulated cores, PS5CoreDiag.h)
			CheckAxGap();
		}
		if (sceAudioOutOutput(m_port, grain.data()) < 0)
		{
			cemuLog_log(LogType::Force, "PS5 AudioOut: output failed, audio stops");
			return;
		}
	}
}

void PS5AudioAPI::RaisePriority()
{
	void* self = scePthreadSelf();
	int was = 0;
	scePthreadGetprio(self, &was);
	if (was > 0 && was <= kAudioPriority)
		return;
	const int result = scePthreadSetprio(self, kAudioPriority);
	cemuLog_log(LogType::Force, "PS5 AudioOut: output thread priority {} -> {}{}", was, kAudioPriority,
		result == 0 ? std::string() : fmt::format(" refused ({:#x}): it stays", (uint32)result));
}

// One line every 10 s, whatever happened: the queue's fill now, its lowest and the longest wait for
// a whole grain since the last line, and the blocks and grains given up. A drain the producer recovers
// from pads nothing and logs nothing, so the lowest fill and the wait are the only evidence of it.
void PS5AudioAPI::LogCountsLocked()
{
	const auto now = std::chrono::steady_clock::now();
	if (m_lastCountLog != std::chrono::steady_clock::time_point{} && now - m_lastCountLog < std::chrono::seconds(10))
		return;
	const uint32 dropped = m_queue.DroppedBlocks(), padded = m_queue.PaddedGrains();
	const uint32 lowest = m_lowestFrames == 0xFFFFFFFF ? m_queue.Frames() : m_lowestFrames;
	cemuLog_log(LogType::Force, "PS5 AudioOut: queue {} ms (lowest {} ms, grain late by {:.1f} ms, {} dropped, {} padded) since the last line",
		m_queue.Milliseconds(kRate), lowest * 1000 / kRate, m_longestGrainWaitNs / 1e6, dropped - m_loggedDropped, padded - m_loggedPadded);
	m_loggedDropped = dropped;
	m_loggedPadded = padded;
	m_lowestFrames = 0xFFFFFFFF;
	m_longestGrainWaitNs = 0;
	m_lastCountLog = now;
}

// An AX feed gap the queue cannot cover: the output thread plays a grain every ~5.3 ms, so it is the
// cheapest host thread to see the hole the moment it exists, and it reads only atomics the emulated
// cores published (PS5CoreDiag.h). At most one line every 200 ms, so a long stall is a few lines;
// before the queue has reached its target the gap is the start's fill, not a drain.
void PS5AudioAPI::CheckAxGap()
{
	if (!m_settled)
		return;
	const uint64 now = ps5ax::NowNs();
	const ps5ax::FeedStats ax = ps5ax::PeekFeedGap();
	const uint64 gap = std::max(ax.sinceLastFrameNs, ax.sinceLastBlockNs);
	if (gap <= 40000000)
		return;
	if (m_lastAxGapNs && now - m_lastAxGapNs < 200000000)
		return;
	m_lastAxGapNs = now;
	PS5Cemu_LogLine(fmt::format("[audio] no AX frame for {:.1f} ms (no block for {:.1f} ms): {}", ax.sinceLastFrameNs / 1e6,
		ax.sinceLastBlockNs / 1e6, ps5diag::Describe(ps5diag::Take(now))));
}
