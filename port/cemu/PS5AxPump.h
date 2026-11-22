// SPDX-License-Identifier: GPL-3.0-or-later
// PS5Cemu: Cemu's AX output's pacing, the one-update-at-a-time gate around it, and the feed gap it
// keeps (patches/cemu 0065: ax_out.cpp and coreinit_Thread.cpp).
//
// Cemu queues one 3 ms AX frame from its main core's idle loop. bettervr.json's audioAnyCore (on
// unless the file says 0) lets any idle emulated core's loop make that call too, so the output keeps
// its pace while the main core runs or sits in a host call. Two pieces make that safe:
//
// - The gate: a core that finds another already in an update leaves this turn's frame to it, so
//   exactly one caller is inside at a time and Cemu's one-frame-in-flight check is unchanged. A
//   caller never waits on the gate, and the update never enters it again: no reentry, no queue.
// - The pacer: the arithmetic AXOut_update has always used (one frame per 3 ms, 2.9 ms while the
//   backend wants more, never closer than 1.7 ms, the backlog skipped past 216 ms), on Cemu's own
//   clock, from whichever core calls. The schedule is host time, as it was with one caller.
//
// The feed gap is what the diagnostics print: the time between two fed frames and between two blocks
// handed to the backend, and which cores fed them, so a drained queue at a warp can be told from the
// pump having run all along.
//
// None of it holds Cemu state, so tools/tests/test-ax-pump.cpp runs the same code on a PC.

#pragma once

#include <atomic>
#include <cstdint>

namespace ps5ax
{
	// Cemu's clock (ax_out.cpp: now_cached, the TSC when the port turns it on): nanoseconds, the same
	// on every core and thread
	uint64_t NowNs();

	// Whether a core other than the main one may run the pump: bettervr.json's audioAnyCore, read once
	// before a game starts, on unless the file says 0
	bool AnyCore();
	void SetAnyCore(bool on);

	// One caller inside the update at a time. TryEnter answers at once: false means another core's
	// idle loop is in the update, and that one queues this turn's frame.
	class Gate
	{
	public:
		bool TryEnter() { return !m_taken.test_and_set(std::memory_order_acquire); }
		void Leave() { m_taken.clear(std::memory_order_release); }

	private:
		std::atomic_flag m_taken = ATOMIC_FLAG_INIT;
	};

	// When the next AX frame is due, as AXOut_update has paced it. Only one caller at a time uses it
	// (the gate), so it holds no lock of its own.
	class Pacer
	{
	public:
		// nowNs: Cemu's clock now, well past one interval (the TSC or CLOCK_MONOTONIC counts from boot)
		Pacer(uint64_t nowNs, uint64_t intervalNs)
			: m_nextFrame(nowNs - intervalNs), m_lastFrame(nowNs)
		{
		}

		struct Step
		{
			bool frame = false;	 // this call queues a frame
			bool skipped = false; // it was more than skipNs late: the backlog is not replayed
		};

		// intervalNs: 3 ms, or 2.9 ms while the backend wants blocks; minimumNs: 1.7 ms between two
		// frames; skipNs: past this lateness the schedule starts again from now rather than replaying.
		// The clock is monotonic (the TSC or CLOCK_MONOTONIC), so the plain subtraction cannot run back.
		Step Due(uint64_t nowNs, uint64_t intervalNs, uint64_t minimumNs, uint64_t skipNs)
		{
			if (nowNs - m_nextFrame < intervalNs)
				return {};
			if (nowNs - m_lastFrame < minimumNs)
				return {};
			m_lastFrame = nowNs;
			Step step;
			step.frame = true;
			if (nowNs - m_nextFrame >= skipNs)
			{
				step.skipped = true;
				m_nextFrame = nowNs - intervalNs;
			}
			else
				m_nextFrame += intervalNs;
			return step;
		}

	private:
		uint64_t m_nextFrame; // the next frame's place on the 3 ms schedule
		uint64_t m_lastFrame; // when the last frame was fed
	};

	struct FeedStats
	{
		uint64_t longestFrameGapNs = 0; // between two fed frames, since the last take
		uint64_t longestBlockGapNs = 0; // between two blocks handed to the backend, since the last take
		uint64_t sinceLastFrameNs = 0;	// at the moment of the read
		uint64_t sinceLastBlockNs = 0;
		uint32_t frames = 0;
		uint32_t blocks = 0;
		uint32_t skipped = 0;
		uint32_t framesByCore[3] = {};
	};

	// The pump's numbers, written by the one caller the gate lets in, read by the port's log lines on
	// other threads: atomics, and the times never run back.
	class FeedGap
	{
	public:
		void Fed(uint64_t nowNs, int coreIndex)
		{
			Raise(m_longestFrameNs, Gap(m_lastFrameNs, nowNs));
			m_lastFrameNs.store(nowNs, std::memory_order_relaxed);
			m_frames.fetch_add(1, std::memory_order_relaxed);
			if (coreIndex >= 0 && coreIndex < 3)
				m_framesByCore[coreIndex].fetch_add(1, std::memory_order_relaxed);
		}

		void Block(uint64_t nowNs)
		{
			Raise(m_longestBlockNs, Gap(m_lastBlockNs, nowNs));
			m_lastBlockNs.store(nowNs, std::memory_order_relaxed);
			m_blocks.fetch_add(1, std::memory_order_relaxed);
		}

		void Skipped() { m_skipped.fetch_add(1, std::memory_order_relaxed); }

		FeedStats Peek(uint64_t nowNs) const
		{
			FeedStats stats;
			stats.longestFrameGapNs = m_longestFrameNs.load(std::memory_order_relaxed);
			stats.longestBlockGapNs = m_longestBlockNs.load(std::memory_order_relaxed);
			stats.sinceLastFrameNs = Gap(m_lastFrameNs, nowNs);
			stats.sinceLastBlockNs = Gap(m_lastBlockNs, nowNs);
			stats.frames = m_frames.load(std::memory_order_relaxed);
			stats.blocks = m_blocks.load(std::memory_order_relaxed);
			stats.skipped = m_skipped.load(std::memory_order_relaxed);
			for (int core = 0; core < 3; core++)
				stats.framesByCore[core] = m_framesByCore[core].load(std::memory_order_relaxed);
			return stats;
		}

		FeedStats Take(uint64_t nowNs)
		{
			FeedStats stats = Peek(nowNs);
			m_longestFrameNs.store(0, std::memory_order_relaxed);
			m_longestBlockNs.store(0, std::memory_order_relaxed);
			m_frames.store(0, std::memory_order_relaxed);
			m_blocks.store(0, std::memory_order_relaxed);
			m_skipped.store(0, std::memory_order_relaxed);
			for (auto& count : m_framesByCore)
				count.store(0, std::memory_order_relaxed);
			return stats;
		}

	private:
		static uint64_t Gap(const std::atomic<uint64_t>& last, uint64_t nowNs)
		{
			const uint64_t was = last.load(std::memory_order_relaxed);
			return was && nowNs > was ? nowNs - was : 0;
		}

		static void Raise(std::atomic<uint64_t>& longest, uint64_t gap)
		{
			uint64_t held = longest.load(std::memory_order_relaxed);
			while (gap > held && !longest.compare_exchange_weak(held, gap, std::memory_order_relaxed))
			{
			}
		}

		std::atomic<uint64_t> m_lastFrameNs{0}, m_lastBlockNs{0}, m_longestFrameNs{0}, m_longestBlockNs{0};
		std::atomic<uint32_t> m_frames{0}, m_blocks{0}, m_skipped{0};
		std::atomic<uint32_t> m_framesByCore[3]{};
	};

	// ax_out.cpp: the pump's own, and the port's log lines take or peek it (port/app/emulator.cpp's
	// [perf] every 10 s, LatteRenderTarget.cpp's long-frame [stall] line)
	FeedStats PeekFeedGap();
	FeedStats TakeFeedGap();
}
