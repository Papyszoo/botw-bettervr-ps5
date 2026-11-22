// SPDX-License-Identifier: GPL-3.0-or-later
// PS5Cemu: the AX output pump's pacing, its one-caller gate and its feed gap on a PC
// (port/cemu/PS5AxPump.h; the console's ax_out.cpp and coreinit_Thread.cpp use these).
//
// With bettervr.json's audioAnyCore every idle emulated core may ask for the pump. This runs the
// same numbers: one frame per 3 ms on host time (2.9 ms while the backend wants blocks, never closer
// than 1.7 ms, the backlog skipped past 216 ms), exactly one caller in an update at a time, and the
// gap the diagnostics print. The test starts its clock at a second, as the TSC does, because the
// pacer's first frame is due one minimum wait after its first call.

#include "PS5AxPump.h"

#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>

namespace
{
	constexpr uint64_t kMs = 1000000ull;
	constexpr uint64_t kPoll = 20 * 1000ull; // how often an idle loop asks (idleEventsUs 20)
	constexpr uint64_t kStart = 1000 * kMs;	 // the clock a console's TSC or CLOCK_MONOTONIC reads
	constexpr uint64_t kInterval = 3 * kMs;
	constexpr uint64_t kIntervalFast = 2900 * 1000ull;
	constexpr uint64_t kMinimum = 1700 * 1000ull;
	constexpr uint64_t kSkip = 216 * kMs;

	int Fail(const char* what)
	{
		std::fprintf(stderr, "ax pump: %s\n", what);
		return 1;
	}

	struct Run
	{
		uint32_t frames = 0;
		uint32_t skipped = 0;
		uint64_t smallestGap = ~0ull;
		uint64_t largestGap = 0;
		uint64_t lastFrame = 0;
	};

	// calls at every poll from start to end, with intervalNs at each call (the backend's need changes
	// it); a gap is the time between two fed frames
	Run Pace(uint64_t start, uint64_t end, uint64_t intervalNs, bool fastFirstSecond)
	{
		ps5ax::Pacer pacer(start, intervalNs);
		Run run;
		for (uint64_t now = start; now < end; now += kPoll)
		{
			const uint64_t interval = fastFirstSecond && now - start < 1000 * kMs ? kIntervalFast : intervalNs;
			const ps5ax::Pacer::Step step = pacer.Due(now, interval, kMinimum, kSkip);
			if (!step.frame)
				continue;
			if (step.skipped)
				run.skipped++;
			if (run.frames)
			{
				const uint64_t gap = now - run.lastFrame;
				run.smallestGap = gap < run.smallestGap ? gap : run.smallestGap;
				run.largestGap = gap > run.largestGap ? gap : run.largestGap;
			}
			run.lastFrame = now;
			run.frames++;
		}
		return run;
	}
}

int main()
{
	int failures = 0;

	// ten seconds of one frame per 3 ms: 3333 frames, never two closer than the 1.7 ms minimum (the
	// schedule's first frame leaves the phase 1.3 ms short, as Cemu's own first check always did), and
	// no frame later than one poll past its place
	{
		const Run run = Pace(kStart, kStart + 10ull * 1000 * kMs, kInterval, false);
		if (run.frames < 3332 || run.frames > 3334)
			failures += Fail("ten seconds are not one frame per 3 ms");
		if (run.smallestGap < kMinimum)
			failures += Fail("two frames came closer than the 1.7 ms minimum");
		if (run.largestGap > kInterval + kPoll)
			failures += Fail("a frame was later than one poll past its place");
		if (run.skipped)
			failures += Fail("a frame on time was reported as skipped");
	}
	// the first frame comes one minimum wait after the pacer's first call, as AXOut_update's did
	{
		ps5ax::Pacer pacer(kStart, kInterval);
		if (pacer.Due(kStart, kInterval, kMinimum, kSkip).frame)
			failures += Fail("a frame was due at the first call, inside the minimum wait");
		if (pacer.Due(kStart + 1600 * 1000ull, kInterval, kMinimum, kSkip).frame)
			failures += Fail("a frame was due inside the 1.7 ms minimum");
		if (!pacer.Due(kStart + 1700 * 1000ull, kInterval, kMinimum, kSkip).frame)
			failures += Fail("no frame was due at the 1.7 ms minimum");
	}
	// 2.9 ms while the backend wants blocks, 3 ms after: 344 + 3000 frames
	{
		const Run run = Pace(kStart, kStart + 10ull * 1000 * kMs, kInterval, true);
		if (run.frames < 3340 || run.frames > 3350)
			failures += Fail("the fast 2.9 ms refill did not raise the frame count as it should");
		if (run.smallestGap < kMinimum)
			failures += Fail("a fast frame came closer than the 1.7 ms minimum");
	}
	// a 300 ms stall in the caller: one frame when it returns (not a burst of the backlog), and the
	// schedule starts again from there
	{
		ps5ax::Pacer pacer(kStart, kInterval);
		uint64_t last = kStart;
		for (uint64_t now = kStart; now < kStart + 100 * kMs; now += kPoll)
			if (pacer.Due(now, kInterval, kMinimum, kSkip).frame)
				last = now;
		const uint64_t back = last + 300 * kMs;
		const ps5ax::Pacer::Step late = pacer.Due(back, kInterval, kMinimum, kSkip);
		if (!late.frame || !late.skipped)
			failures += Fail("a frame 300 ms late was not skipped forward");
		if (pacer.Due(back + kMinimum, kInterval, kMinimum, kSkip).skipped)
			failures += Fail("the frame after a skip was reported as skipped too");
		uint32_t burst = 0;
		for (uint64_t now = back; now < back + kMinimum; now += kPoll)
			burst += pacer.Due(now, kInterval, kMinimum, kSkip).frame ? 1 : 0;
		if (burst)
			failures += Fail("the skipped backlog was replayed");
	}

	// one caller at a time: a second player (or the same one again) is turned away, and after Leave
	// the gate is free again
	{
		ps5ax::Gate gate;
		if (!gate.TryEnter())
			failures += Fail("a free gate refused the first caller");
		if (gate.TryEnter())
			failures += Fail("the gate let a second caller in");
		gate.Leave();
		if (!gate.TryEnter())
			failures += Fail("the gate stayed taken after Leave");
		gate.Leave();
	}
	// three cores asking at once: every entry is alone, and none is lost or doubled
	{
		ps5ax::Gate gate;
		std::atomic<uint32_t> inside{0}, total{0};
		std::atomic<bool> overlapped{false};
		constexpr int kThreads = 3, kTurns = 200000;
		std::vector<std::thread> threads;
		for (int thread = 0; thread < kThreads; thread++)
			threads.emplace_back([&] {
				for (int turn = 0; turn < kTurns; turn++)
				{
					if (!gate.TryEnter())
						continue; // another core is in the update: this turn is not ours
					if (inside.fetch_add(1, std::memory_order_relaxed) != 0)
						overlapped = true;
					total.fetch_add(1, std::memory_order_relaxed);
					inside.fetch_sub(1, std::memory_order_relaxed);
					gate.Leave();
				}
			});
		for (auto& thread : threads)
			thread.join();
		if (overlapped)
			failures += Fail("two cores were inside one update");
		if (total.load() == 0)
			failures += Fail("no core ever got in");
	}

	// the gap the diagnostics print: frames every 3 ms and blocks every 12 ms, with a 72 ms hole,
	// and which core fed each frame
	{
		ps5ax::FeedGap gap;
		uint64_t now = kStart;
		for (int frame = 0; frame < 40; frame++, now += kInterval)
		{
			if (frame == 20)
				now += 72 * kMs - kInterval; // the pump stopped between frames 19 and 20
			gap.Fed(now, frame % 3);
			if (frame % 4 == 3)
				gap.Block(now);
		}
		gap.Skipped();
		const uint64_t lastFed = now - kInterval; // the loop's last step
		const ps5ax::FeedStats stats = gap.Peek(lastFed + kMs);
		if (stats.frames != 40 || stats.blocks != 10)
			failures += Fail("the feed counts are not the frames and blocks fed");
		if (stats.skipped != 1)
			failures += Fail("a skipped schedule was not counted");
		if (stats.longestFrameGapNs < 72 * kMs)
			failures += Fail("the 72 ms hole is not the longest gap between frames");
		if (stats.longestBlockGapNs < 72 * kMs)
			failures += Fail("the 72 ms hole is not the longest gap between blocks");
		if (stats.framesByCore[0] + stats.framesByCore[1] + stats.framesByCore[2] != 40)
			failures += Fail("the per-core counts do not add up to the frames fed");
		if (stats.sinceLastBlockNs != kMs || stats.sinceLastFrameNs != kMs)
			failures += Fail("the time since the last frame or block is not the read's distance");
		const ps5ax::FeedStats taken = gap.Take(lastFed + kMs);
		if (taken.longestFrameGapNs != stats.longestFrameGapNs || taken.frames != 40)
			failures += Fail("Take did not hand over the numbers it read");
		const ps5ax::FeedStats after = gap.Peek(lastFed + kMs);
		if (after.longestFrameGapNs || after.longestBlockGapNs || after.frames || after.blocks || after.skipped ||
			after.framesByCore[0] || after.framesByCore[1] || after.framesByCore[2])
			failures += Fail("Take did not start the window over");
	}

	if (failures)
		return 1;
	std::printf("ax pump: one frame per 3 ms, one caller at a time, and the gap measured\n");
	return 0;
}
