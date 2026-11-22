// SPDX-License-Identifier: GPL-3.0-or-later
// PS5Cemu: the AX output's feed gap, what a warp's drained queue is made of (patches/cemu 0065:
// ax_out.cpp's AIInitDMA and AXOut_update).
//
// Cemu queues one 3 ms AX frame from its main core's idle loop (coreinit_Thread.cpp's
// __OSCheckSystemEvents), the AX thread mixes it, and every fourth frame hands one block to the
// PS5's audio queue. The pump's schedule is Cemu's own, unchanged: 3 ms a frame, 2.9 ms while the
// backend wants blocks, never closer than 1.7 ms, the backlog skipped past 216 ms.
//
// The gap between two fed frames and between two blocks is what the port's lines print: a hole in
// the output can then be told from a queue that never drained. Written by the update on core 1,
// read by the port's log lines on other threads, so atomics and times that never run back.

#pragma once

#include <atomic>
#include <cstdint>

namespace ps5ax
{
	// Cemu's clock (ax_out.cpp: now_cached, the TSC when the port turns it on): nanoseconds, the same
	// on every core and thread
	uint64_t NowNs();

	struct FeedStats
	{
		uint64_t longestFrameGapNs = 0; // between two fed frames, since the last take
		uint64_t longestBlockGapNs = 0; // between two blocks handed to the backend, since the last take
		uint64_t sinceLastFrameNs = 0;	// at the moment of the read
		uint64_t sinceLastBlockNs = 0;
		uint32_t frames = 0;
		uint32_t blocks = 0;
		uint32_t skipped = 0; // the update was past its 216 ms skip and did not replay the backlog
	};

	// The pump's numbers, written by the update on core 1, read by the port's log lines on other
	// threads: atomics, and the times never run back.
	class FeedGap
	{
	public:
		void Fed(uint64_t nowNs)
		{
			Raise(m_longestFrameNs, Gap(m_lastFrameNs, nowNs));
			m_lastFrameNs.store(nowNs, std::memory_order_relaxed);
			m_frames.fetch_add(1, std::memory_order_relaxed);
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
	};

	// ax_out.cpp: the pump's own, and the port's log lines take or peek it (port/app/emulator.cpp's
	// [perf] every 10 s, LatteRenderTarget.cpp's long-frame [stall] line, PS5AudioAPI.cpp's gap line)
	FeedStats PeekFeedGap();
	FeedStats TakeFeedGap();
}
