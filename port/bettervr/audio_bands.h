// SPDX-License-Identifier: GPL-3.0-or-later
// The v0.8 haptic director's ear (docs/bettervr-ps5/STATUS.md, the v0.8 haptics entry): three band envelopes and
// two onset rates from Cemu's TV output, computed on Cemu's audio thread and published as atomics the existing
// 60 Hz mixer reads. The footstep classifier captures a short window after the mixer's predicted footfall, so it
// hears the step's transient rather than the whole mix. No allocation, no lock and no logging in the hot path;
// PS5AudioAPI::FeedBlock calls Analyze after it has released its queue mutex.
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace ps5audio {
constexpr float kLowHz = 60.0f;
constexpr float kMidHz = 500.0f;
constexpr float kHighHz = 4000.0f;
constexpr float kPi = 3.14159265358979323846f;
constexpr float kNormGain = 5.0f; // a quiet ambience still reaches a usable 0..1 level
constexpr float kStepWindowSeconds = 0.16f;
constexpr int kStepBlocks = 48;

struct Snapshot {
	float low = 0.0f;
	float mid = 0.0f;
	float high = 0.0f;
	float onsets = 0.0f;
	float micro = 0.0f;
	uint32_t thunder = 0;
};

struct StepResult {
	uint32_t generation = 0;
	bool valid = false;
	float low = 0.0f;
	float mid = 0.0f;
	float high = 0.0f;
	float hardness = 0.0f;
	float grain = 0.0f;
	float decay = 0.0f;
};

// the only state the audio thread and the mixer share
struct SharedState {
	std::atomic<float> low{ 0.0f };
	std::atomic<float> mid{ 0.0f };
	std::atomic<float> high{ 0.0f };
	std::atomic<float> onsets{ 0.0f };
	std::atomic<float> micro{ 0.0f };
	std::atomic<uint32_t> thunder{ 0 };
	std::atomic<uint32_t> stepRequest{ 0 };
	std::atomic<uint32_t> stepReady{ 0 };
	std::atomic<bool> stepValid{ false };
	std::atomic<float> stepLow{ 0.0f };
	std::atomic<float> stepMid{ 0.0f };
	std::atomic<float> stepHigh{ 0.0f };
	std::atomic<float> stepHardness{ 0.0f };
	std::atomic<float> stepGrain{ 0.0f };
	std::atomic<float> stepDecay{ 0.0f };

	Snapshot Now() const {
		Snapshot s;
		s.low = low.load(std::memory_order_relaxed);
		s.mid = mid.load(std::memory_order_relaxed);
		s.high = high.load(std::memory_order_relaxed);
		s.onsets = onsets.load(std::memory_order_relaxed);
		s.micro = micro.load(std::memory_order_relaxed);
		s.thunder = thunder.load(std::memory_order_relaxed);
		return s;
	}
};

inline SharedState& Shared() {
	static SharedState state;
	return state;
}

// asks the audio thread for the next 160 ms after this tick's footfall; the returned generation names the result
inline uint32_t RequestStep() {
	return Shared().stepRequest.fetch_add(1, std::memory_order_relaxed) + 1;
}

inline bool StepReady(uint32_t generation) {
	return generation != 0 && Shared().stepReady.load(std::memory_order_acquire) == generation;
}

inline StepResult Step(uint32_t generation) {
	StepResult r;
	if (!StepReady(generation))
		return r;
	r.generation = generation;
	r.valid = Shared().stepValid.load(std::memory_order_relaxed);
	r.low = Shared().stepLow.load(std::memory_order_relaxed);
	r.mid = Shared().stepMid.load(std::memory_order_relaxed);
	r.high = Shared().stepHigh.load(std::memory_order_relaxed);
	r.hardness = Shared().stepHardness.load(std::memory_order_relaxed);
	r.grain = Shared().stepGrain.load(std::memory_order_relaxed);
	r.decay = Shared().stepDecay.load(std::memory_order_relaxed);
	return r;
}

// RBJ bandpass with a 0 dB peak: five multiplies a sample, no allocation
struct Biquad {
	float b0 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
	float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;

	void Set(float hz, float q, float rate) {
		const float w = 2.0f * kPi * hz / rate;
		const float alpha = std::sin(w) / (2.0f * q);
		const float a0 = 1.0f + alpha;
		b0 = alpha / a0;
		b2 = -alpha / a0;
		a1 = (-2.0f * std::cos(w)) / a0;
		a2 = (1.0f - alpha) / a0;
	}
	void Reset() {
		x1 = x2 = y1 = y2 = 0.0f;
	}
	float Process(float x) {
		const float y = b0 * x + b2 * x2 - a1 * y1 - a2 * y2;
		x2 = x1;
		x1 = x;
		y2 = y1;
		y1 = y;
		return y;
	}
};

// a block-rate envelope follower: attack and release are time constants, not per-sample exps
struct Follower {
	float value = 0.0f;
	void Reset() {
		value = 0.0f;
	}
	void Update(float target, float dt, float attack, float release) {
		const float tau = target > value ? attack : release;
		const float a = 1.0f - std::exp(-dt / tau);
		value += (target - value) * a;
	}
};

class Analyzer {
public:
	void Configure(uint32_t rate) {
		if (rate == m_rate && m_configured)
			return;
		m_rate = rate;
		m_low.Set(kLowHz, 0.70f, (float)rate);
		m_mid.Set(kMidHz, 0.90f, (float)rate);
		m_high.Set(kHighHz, 0.80f, (float)rate);
		m_low.Reset();
		m_mid.Reset();
		m_high.Reset();
		m_broadFast.Reset();
		m_broadSlow.Reset();
		m_highFast.Reset();
		m_highSlow.Reset();
		m_lowFast.Reset();
		m_lowSlow.Reset();
		m_time = 0.0f;
		m_lastOnset = -1.0f;
		m_lastMicro = -1.0f;
		m_lastThunder = -1.0f;
		m_seenRequest = Shared().stepRequest.load(std::memory_order_relaxed);
		m_capture = false;
		m_configured = true;
	}

	void Block(const int16_t* data, uint32_t frames, uint32_t channels) {
		if (data == nullptr || frames == 0 || channels == 0 || m_rate == 0)
			return;
		const float dt = (float)frames / (float)m_rate;
		m_time += dt;

		float sumLow = 0.0f, sumMid = 0.0f, sumHigh = 0.0f, sumBroad = 0.0f;
		const int16_t* p = data;
		for (uint32_t i = 0; i < frames; i++) {
			float left, right;
			if (channels >= 2) {
				left = p[0];
				right = p[1];
				p += channels;
			} else {
				left = right = p[0];
				p++;
			}
			const float mono = (left + right) * (0.5f / 32768.0f);
			sumLow += std::fabs(m_low.Process(mono));
			sumMid += std::fabs(m_mid.Process(mono));
			sumHigh += std::fabs(m_high.Process(mono));
			sumBroad += std::fabs(mono);
		}

		const float low = sumLow / (float)frames;
		const float mid = sumMid / (float)frames;
		const float high = sumHigh / (float)frames;
		const float broad = sumBroad / (float)frames;

		m_broadFast.Update(broad, dt, 0.002f, 0.020f);
		m_broadSlow.Update(broad, dt, 0.020f, 0.350f);
		m_highFast.Update(high, dt, 0.002f, 0.015f);
		m_highSlow.Update(high, dt, 0.020f, 0.100f);
		m_lowFast.Update(low, dt, 0.002f, 0.030f);
		m_lowSlow.Update(low, dt, 0.020f, 0.400f);

		// onsets: the broadband one is an impact, the high-band one is the grain in rain and sand
		if (m_time - m_lastOnset > 0.060f && m_broadFast.value > m_broadSlow.value * 1.60f + 0.010f &&
		    m_broadFast.value > 0.015f) {
			m_onsetRate += 1.0f;
			m_lastOnset = m_time;
		}
		if (m_time - m_lastMicro > 0.030f && m_highFast.value > m_highSlow.value * 1.20f + 0.004f &&
		    m_highFast.value > 0.006f) {
			m_microRate += 1.0f;
			m_lastMicro = m_time;
		}
		if (m_time - m_lastThunder > 4.0f && m_lowFast.value > m_lowSlow.value * 2.20f + 0.030f &&
		    m_lowFast.value > 0.080f) {
			Shared().thunder.fetch_add(1, std::memory_order_relaxed);
			m_lastThunder = m_time;
		}
		m_onsetRate *= std::exp(-dt);
		m_microRate *= std::exp(-dt / 0.80f);

		Shared().low.store(Norm(low), std::memory_order_relaxed);
		Shared().mid.store(Norm(mid), std::memory_order_relaxed);
		Shared().high.store(Norm(high), std::memory_order_relaxed);
		Shared().onsets.store(m_onsetRate, std::memory_order_relaxed);
		Shared().micro.store(m_microRate / 0.80f, std::memory_order_relaxed);

		CaptureBlock(low, mid, high, dt);
	}

private:
	static float Norm(float envelope) {
		return std::sqrt(std::clamp(envelope * kNormGain, 0.0f, 1.0f));
	}

	void CaptureBlock(float low, float mid, float high, float dt) {
		const uint32_t request = Shared().stepRequest.load(std::memory_order_relaxed);
		if (request != m_seenRequest) {
			m_seenRequest = request;
			m_capture = true;
			m_captureGeneration = request;
			m_captureElapsed = 0.0f;
			m_stepCount = 0;
		}
		if (!m_capture)
			return;

		if (m_stepCount < kStepBlocks) {
			m_stepLow[m_stepCount] = low;
			m_stepMid[m_stepCount] = mid;
			m_stepHigh[m_stepCount] = high;
			m_stepCount++;
		}
		m_captureElapsed += dt;
		if (m_captureElapsed < kStepWindowSeconds && m_stepCount < kStepBlocks)
			return;

		m_capture = false;
		float peakLow = 0.0f, peakMid = 0.0f, peakHigh = 0.0f;
		float peakSum = 0.0f;
		int peakIndex = 0;
		for (int i = 0; i < m_stepCount; i++) {
			peakLow = std::max(peakLow, m_stepLow[i]);
			peakMid = std::max(peakMid, m_stepMid[i]);
			peakHigh = std::max(peakHigh, m_stepHigh[i]);
			const float sum = m_stepLow[i] + m_stepMid[i] + m_stepHigh[i];
			if (sum > peakSum) {
				peakSum = sum;
				peakIndex = i;
			}
		}
		if (peakSum < 0.010f) {
			Shared().stepValid.store(false, std::memory_order_relaxed);
			Shared().stepReady.store(m_captureGeneration, std::memory_order_release);
			return; // an invalid result: the director keeps the surface it had
		}

		const int earlyEnd = std::min(peakIndex + 3, m_stepCount);
		const int lateStart = std::min(peakIndex + 6, m_stepCount - 1);
		const int lateEnd = std::min(peakIndex + 9, m_stepCount);
		float early = 0.0f, late = 0.0f;
		int earlyCount = 0, lateCount = 0;
		for (int i = peakIndex; i < earlyEnd; i++, earlyCount++)
			early += m_stepLow[i] + m_stepMid[i] + m_stepHigh[i];
		for (int i = lateStart; i < lateEnd; i++, lateCount++)
			late += m_stepLow[i] + m_stepMid[i] + m_stepHigh[i];
		if (earlyCount > 0)
			early /= (float)earlyCount;
		if (lateCount > 0)
			late /= (float)lateCount;

		const float total = peakLow + peakMid + peakHigh + 1.0e-4f;
		const float hardness = std::clamp((peakHigh + 0.5f * peakMid) / total, 0.0f, 1.0f);
		const float grain = std::clamp(peakHigh / (peakMid + 0.02f), 0.0f, 1.0f);
		const float decay = std::clamp(1.0f - late / (early + 1.0e-4f), 0.0f, 1.0f);

		Shared().stepLow.store(peakLow, std::memory_order_relaxed);
		Shared().stepMid.store(peakMid, std::memory_order_relaxed);
		Shared().stepHigh.store(peakHigh, std::memory_order_relaxed);
		Shared().stepHardness.store(hardness, std::memory_order_relaxed);
		Shared().stepGrain.store(grain, std::memory_order_relaxed);
		Shared().stepDecay.store(decay, std::memory_order_relaxed);
		Shared().stepValid.store(true, std::memory_order_relaxed);
		Shared().stepReady.store(m_captureGeneration, std::memory_order_release);
	}

	uint32_t m_rate = 0;
	bool m_configured = false;
	float m_time = 0.0f;
	Biquad m_low, m_mid, m_high;
	Follower m_broadFast, m_broadSlow, m_highFast, m_highSlow, m_lowFast, m_lowSlow;
	float m_onsetRate = 0.0f;
	float m_microRate = 0.0f;
	float m_lastOnset = -1.0f;
	float m_lastMicro = -1.0f;
	float m_lastThunder = -1.0f;

	bool m_capture = false;
	uint32_t m_seenRequest = 0;
	uint32_t m_captureGeneration = 0;
	float m_captureElapsed = 0.0f;
	int m_stepCount = 0;
	std::array<float, kStepBlocks> m_stepLow{}, m_stepMid{}, m_stepHigh{};
};

inline Analyzer& ThreadAnalyzer(uint32_t rate) {
	thread_local Analyzer analyzer;
	analyzer.Configure(rate);
	return analyzer;
}

inline void Analyze(const int16_t* data, uint32_t frames, uint32_t channels, uint32_t rate) {
	if (rate == 0)
		return;
	ThreadAnalyzer(rate).Block(data, frames, channels);
}
}
