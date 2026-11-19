// SPDX-License-Identifier: GPL-3.0-or-later
// PS5Cemu: Cemu's audio queue on a PC, as the console feeds and drains it (port/cemu/PS5AudioQueue.h).
//
// The old sizing kept two of Cemu's 12 ms blocks (24 ms) waiting and padded a grain that was short by
// a few frames with silence: a splice, heard as a click. This runs the same producer (an AX frame
// every 3 ms, 4 frames to a block, the fast 2.9 ms refill while the queue is under its target) and
// the same consumer (a 256-frame grain every 5.33 ms) against both. The four-block target must ride
// out a hitch in the game's audio thread and two minutes 300 ppm apart, with no block dropped, no
// grain padded and no grain slot left empty.

#include "PS5AudioQueue.h"

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <vector>

namespace
{
	constexpr uint32_t kChannels = 2;
	constexpr uint32_t kBlock = 576; // Cemu's 4 AX frames of 144
	constexpr uint32_t kGrain = 256; // frames per sceAudioOutOutput
	constexpr double kRate = 48000.0;
	constexpr double kGrainMs = kGrain * 1000.0 / kRate;
	constexpr double kStepMs = 0.02; // how often the console looks at the game's events

	struct Result
	{
		uint32_t dropped = 0;	  // blocks Cemu fed and the queue gave up
		uint32_t shortGrains = 0; // grains with fewer than kGrain frames, padded (the old policy only)
		uint32_t silentSlots = 0; // grain slots the device got nothing for: a dropout, not a click
		uint32_t minimumFrames = 0;
	};

	// pad: the old policy plays whatever is queued and fills the rest with silence. Without it, a
	// grain is played only when a whole one is queued, as OutputThread now does.
	Result Run(uint32_t targetBlocks, uint32_t capacityBlocks, bool pad, double ppm, double hitchMs, double hitchAtMs, double seconds)
	{
		PS5AudioQueue queue(kChannels, kBlock, kGrain, targetBlocks * kBlock, capacityBlocks * kBlock);
		std::vector<int16_t> block(kBlock * kChannels, 0);
		Result result;
		result.minimumFrames = targetBlocks * kBlock;

		double now = 0, nextAx = 0, nextGrain = 0;
		uint32_t framesInBlock = 0;
		while (now < seconds * 1000.0)
		{
			// a hitch: the game's audio thread is busy; what it did not make, it makes as soon as it
			// runs again (its timer kept its place, so the AX frames queue back to back)
			const bool hitched = hitchMs > 0 && now >= hitchAtMs && now < hitchAtMs + hitchMs;
			if (!hitched && now >= nextAx)
			{
				// Cemu's AXOut_update: the refill is faster while the queue wants blocks, and never
				// sooner than 1.7 ms after the last frame (its catch-up after a hitch runs at that)
				const double interval = (queue.NeedsMore() ? 2.9 : 3.0) * (1.0 + ppm / 1e6);
				nextAx = std::max(nextAx + interval, now + 1.7);
				if (++framesInBlock == 4)
				{
					queue.Push(block.data());
					framesInBlock = 0;
				}
			}
			if (now >= nextGrain)
			{
				nextGrain += kGrainMs;
				const uint32_t frames = (uint32_t)queue.Frames();
				const bool counted = now >= 200; // the first block takes a moment to arrive: not a fault
				if (pad && frames < kGrain)
				{
					if (counted)
						result.shortGrains++; // the tail padded with silence: the click (a whole grain at 0)
					queue.Consume(frames);
				}
				else if (frames >= kGrain)
					queue.Consume(kGrain);
				else if (counted)
					result.silentSlots++; // the thread waits; the device had nothing to play
				if (counted)
					result.minimumFrames = std::min(result.minimumFrames, (uint32_t)queue.Frames());
			}
			now += kStepMs;
		}
		result.dropped = queue.DroppedBlocks();
		return result;
	}

	int Fail(const char* what, const Result& r)
	{
		std::fprintf(stderr, "%s: %u blocks dropped, %u short grains, %u silent slots (lowest fill %u frames)\n", what, r.dropped,
			r.shortGrains, r.silentSlots, r.minimumFrames);
		return 1;
	}
}

int main()
{
	int failures = 0;

	// steady play, the two clocks on top of each other
	{
		const Result r = Run(4, 12, false, 0, 0, 0, 30);
		if (r.dropped || r.shortGrains || r.silentSlots)
			failures += Fail("steady play", r);
	}
	// a 30 ms hitch in the game's audio thread once the queue is settled: the old two-block target
	// (24 ms) drains under a grain and pads it, four blocks ride it out
	{
		const Result old = Run(2, 24, true, 0, 30, 1000, 10);
		if (old.shortGrains == 0)
			failures += Fail("the old sizing should have padded a grain", old);
		const Result now = Run(4, 12, false, 0, 30, 1000, 10);
		if (now.dropped || now.shortGrains || now.silentSlots)
			failures += Fail("a 30 ms hitch", now);
	}
	// the two clocks 300 ppm apart for two minutes: the fill must stay in its band, with no
	// correction of any kind
	for (const double ppm : {-300.0, 300.0})
	{
		const Result r = Run(4, 12, false, ppm, 0, 0, 120);
		if (r.dropped || r.shortGrains || r.silentSlots)
			failures += Fail(ppm < 0 ? "two minutes 300 ppm slow" : "two minutes 300 ppm fast", r);
		if (r.minimumFrames < kGrain)
			failures += Fail("the queue ran under a grain", r);
	}

	if (failures)
		return 1;
	std::printf("audio queue: no block dropped and no grain padded in steady play\n");
	return 0;
}
