// SPDX-License-Identifier: MPL-2.0
// PS5Cemu: the samples waiting for the PS5's AudioOut (port/cemu/PS5AudioAPI.cpp).
//
// Cemu hands over blocks of samples_per_block frames (12 ms at 48 kHz for the TV's sound). AudioOut
// plays grains of 256 frames (5.33 ms), which do not divide a block, and the stream's clock and the
// console's differ by a few tens of parts per million. A grain is played only once a whole one has
// arrived: a short grain padded with silence is a splice, heard as a click every time the two clocks'
// phases cross (the 2 Hz click of v0.6). The queue keeps whole blocks ready, so the output thread
// never waits on Cemu in steady play, and it keeps the newest sound when a pause fills it.
//
// Only the audio output's lock guards this, so it has none of its own; the counts are safe to read
// under the same lock.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

class PS5AudioQueue
{
public:
	PS5AudioQueue() = default;
	PS5AudioQueue(uint32_t channels, uint32_t blockFrames, uint32_t grainFrames, uint32_t targetFrames, uint32_t capacityFrames)
		: m_channels(channels), m_blockFrames(blockFrames), m_grainFrames(grainFrames), m_targetFrames(targetFrames), m_capacityFrames(capacityFrames)
	{
		m_samples.reserve((size_t)capacityFrames * channels);
	}

	// one block from Cemu: the oldest sound gives way when a stall filled the queue, so what plays on
	// the way back is the sound of now, not of the stall
	bool Push(const int16_t* data)
	{
		const size_t block = (size_t)m_blockFrames * m_channels;
		if (!Fits(block))
		{
			const size_t keep = std::min(m_samples.size(), (size_t)m_targetFrames * m_channels);
			const size_t lost = m_samples.size() - keep;
			m_samples.erase(m_samples.begin(), m_samples.end() - keep);
			m_droppedBlocks += (uint32_t)std::max<size_t>(1, (lost + block - 1) / block);
			if (!Fits(block))
				m_samples.clear();
		}
		m_samples.insert(m_samples.end(), data, data + block);
		return true;
	}

	const int16_t* Data() const { return m_samples.data(); }
	void Consume(size_t frames) { m_samples.erase(m_samples.begin(), m_samples.begin() + frames * m_channels); }
	void Clear() { m_samples.clear(); }

	size_t Frames() const { return m_samples.size() / m_channels; }
	bool HasGrain() const { return Frames() >= m_grainFrames; }
	bool NeedsMore() const { return Frames() < m_targetFrames; }
	uint32_t Milliseconds(uint32_t rate) const { return (uint32_t)(Frames() * 1000 / rate); }

	uint32_t DroppedBlocks() const { return m_droppedBlocks; }
	uint32_t PaddedGrains() const { return m_paddedGrains; }
	void CountPaddedGrain() { m_paddedGrains++; }

private:
	bool Fits(size_t samples) const { return m_samples.size() + samples <= (size_t)m_capacityFrames * m_channels; }

	uint32_t m_channels = 2;
	uint32_t m_blockFrames = 576;
	uint32_t m_grainFrames = 256;
	uint32_t m_targetFrames = 2304;
	uint32_t m_capacityFrames = 6912;
	std::vector<int16_t> m_samples;
	uint32_t m_droppedBlocks = 0;
	uint32_t m_paddedGrains = 0;
};
