// SPDX-License-Identifier: GPL-3.0-or-later
// PS5 haptic director: the shape of the v0.8 effects, kept free of the PS5 headers so tools/tests can check the
// numbers on a PC. A level is 0..1 before its row's strength (utils/mod_settings.h) is applied; the director
// multiplies by that, then the mixer takes the louder of everything whose row is on.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ps5hapt {
// The PlayerNormal states the v0.6/v0.7 console logs already hold (docs/bettervr-ps5/STATUS.md and the v0.8
// haptics entry). The hashes are the game's; the names in the logs are Japanese, e.g. 坂滑り for the surf.
constexpr uint32_t kShieldSurf = 0x101E0498;
constexpr uint32_t kShieldSurfLanding = 0x101E0990;
constexpr uint32_t kGlide = 0x101E06AC;
constexpr uint32_t kSwim = 0x101E08B0;
constexpr uint32_t kClimb = 0x101E0390;
constexpr uint32_t kLadder = 0x101E0658;
// the dodge states: a flurry follows a perfect one, so the parry/flurry probe opens its window on these
constexpr uint32_t kBackflip = 0x101E04BC;
constexpr uint32_t kSideStep = 0x101E04A4;

inline float Clamp01(float value) {
	return std::clamp(value, 0.0f, 1.0f);
}

// a speed or a rate, from 0 at nothing to 1 at `full`
inline float Unit(float value, float full) {
	return full <= 0.0f ? 0.0f : Clamp01(value / full);
}

// Shield Surf: a low scrape that rises with speed. 2 m/s is a crawl, 18 m/s a full slope. The landing is one
// thump scaled by the speed that was left; a headset tick is only for a drop at or above kSurfLandingHeadsetSpeed.
inline float SurfLevel(float speed) {
	return 0.18f + 0.42f * Unit(speed, 18.0f);
}
inline float SurfLandingLevel(float speed) {
	return 0.25f + 0.50f * Unit(speed, 18.0f);
}
inline constexpr float kSurfLandingHeadsetSpeed = 12.0f; // m/s
inline constexpr float kSurfLandingSeconds = 0.35f;

// Paraglider: a soft wind bed, louder when diving. The headset gets rare gusts only, never a bed.
inline float GlideLevel(float speed, float descent) {
	return std::min(0.45f, 0.10f + 0.22f * Unit(speed, 18.0f) + 0.18f * Unit(descent, 10.0f));
}
inline constexpr float kGlideGustStrength = 0.42f; // fixed before the Headset Limit, not the row's level; 300-600 ms

// Swim: an entry splash, a submerged rush that rises with speed, and an exit drip.
inline float SwimRushLevel(float speed) {
	return 0.08f + 0.30f * Unit(speed, 6.0f);
}
inline constexpr float kSplashLevel = 0.50f;
inline constexpr float kSplashSeconds = 0.45f;
inline constexpr float kDripLevel = 0.18f;
inline constexpr float kDripSeconds = 0.25f;

// Climb: hand over hand, faster with the vertical speed; each step lands on one hand.
inline float ClimbStepHz(float vertical) {
	return 1.10f + 1.10f * Unit(vertical, 1.2f);
}
inline float ClimbLevel(float vertical) {
	return 0.22f + 0.25f * Unit(vertical, 1.2f);
}
inline constexpr float kClimbStepSeconds = 0.18f;

// Gallop: a 2-4 Hz cadence, stronger with speed; the legs alternate hands.
inline float GallopHz(float speed) {
	return 2.0f + 2.0f * Unit(speed, 12.0f);
}
inline float GallopLevel(float speed) {
	return 0.30f + 0.30f * Unit(speed, 12.0f);
}
inline constexpr float kGallopStepSeconds = 0.20f;

// Low health: one beat per 1.1 s at half life, 0.55 s near death; above half life there is no beat. The hands get
// the lower thump and the headset a continuous level for the beat's length (a body signal, below Damage Taken's
// pulse). At exactly half the beat is on; above it there is none.
inline constexpr float kHeartLifeHalf = 0.5f;
inline float HeartPeriod(float lifeFraction) {
	if (lifeFraction > kHeartLifeHalf)
	    return 0.0f;
	return 1.10f - 0.55f * Unit(kHeartLifeHalf - lifeFraction, kHeartLifeHalf);
}
inline float HeartHandLevel(float lifeFraction) {
	return 0.30f + 0.35f * Unit(kHeartLifeHalf - lifeFraction, kHeartLifeHalf);
}
inline float HeartHeadLevel(float lifeFraction) {
	return 0.70f + 0.25f * Unit(kHeartLifeHalf - lifeFraction, kHeartLifeHalf);
}
inline constexpr float kBeatSeconds = 0.18f;

// a beat: a short attack, then a squared decay; 0 at both ends
inline float BeatEnvelope(float progress) {
	const float p = Clamp01(progress);
	constexpr float attack = 0.18f;
	if (p < attack)
	    return p / attack;
	const float decay = (p - attack) / (1.0f - attack);
	return (1.0f - decay) * (1.0f - decay);
}

// a one-shot at impact: full at the start, then a squared decay
inline float FallingEnvelope(float progress) {
	const float left = 1.0f - Clamp01(progress);
	return left * left;
}

// Weather & Water: the audio detector's three band levels (0..1) and its micro-onset rate (per second). The
// heuristics are deliberately simple and conservative; the [vib] env lines are what a console run tunes.
struct WeatherMix {
	float rain = 0.0f;
	float water = 0.0f;
	float wind = 0.0f;
};
inline WeatherMix WeatherFromBands(float low, float mid, float high, float microOnsets) {
	const float rain = Clamp01((high - 0.18f) * 1.6f) * Clamp01(microOnsets / 10.0f);
	const float water = Clamp01((mid * 1.2f + low * 0.5f - 0.28f) * 1.5f) * (1.0f - 0.65f * rain);
	const float wind = Clamp01((low * 1.5f + mid * 0.25f - 0.22f) * 1.7f) * (1.0f - 0.50f * rain) *
	                   (1.0f - 0.60f * water);
	return {rain, water, wind};
}
// the rain bed is a randomised tick train, never one event per drop: 6-30 ticks a second, each a short tap
inline float RainTickHz(float rain) {
	return 6.0f + 24.0f * Clamp01(rain);
}
inline constexpr float kRainTickSeconds = 0.07f;
inline float RainTickLevel(float rain, float random) {
	return (0.35f + 0.45f * Clamp01(rain)) * (0.55f + 0.45f * Clamp01(random));
}
inline float RainBedLevel(float rain) {
	return 0.25f + 0.45f * Clamp01(rain);
}
inline float WaterBedLevel(float water) {
	return 0.28f + 0.50f * Clamp01(water);
}
inline float WindBedLevel(float wind) {
	return 0.25f + 0.45f * Clamp01(wind);
}
// thunder keeps a fixed direct-pulse strength before the Headset Limit; the row's Headset tick gates it
inline constexpr float kThunderHeadsetStrength = 1.0f; // 25 Hz at a 100 % limit through the runtime's sqrt mapping
inline constexpr int kThunderHeadsetMs = 180;
inline constexpr float kThunderHandLevel = 0.55f;
inline constexpr float kThunderHandSeconds = 0.30f;
inline constexpr float kThunderMinSeconds = 6.0f;

// Footsteps & Ground: the walk states in the console logs (移動 and the side/step-up variants). Unknown states
// do not step: a guess here would tick through a glide or a cutscene.
inline bool IsWalkState(uint32_t state) {
	switch (state) {
	case 0x101E064C: // 移動
	case 0x101E06D0: // 移動
	case 0x101E08C0: // 移動
	case 0x101E04A4: // サイドステップ
	case 0x101E069C: // 段差登り
	case 0x101E0E84: // 段差登り
		return true;
	default:
		return false;
	}
}
// one step per stride, and the stride grows with speed, so the cadence follows distance rather than a timer
inline float StepStride(float speed) {
	return 0.70f + 0.50f * Unit(speed, 6.0f);
}
inline float StepRateHz(float speed) {
	if (speed <= 0.35f)
		return 0.0f;
	return std::min(4.5f, speed / StepStride(speed));
}
inline float StepLevel(float speed) {
	return 0.28f + 0.24f * Unit(speed, 6.0f);
}

// the material layer's two coordinates: hardness (spectral balance) and grain (high over mid), plus the
// transient's decay. They move timbre continuously instead of naming a surface.
struct SurfaceTone {
	float level = 0.34f;
	float seconds = 0.14f;
	float largeShare = 1.0f;
	float smallShare = 0.35f;
	float bed = 0.04f;
};
inline SurfaceTone SurfaceToneFrom(float hardness, float grain, float decay) {
	const float h = Clamp01(hardness);
	const float g = Clamp01(grain);
	const float d = Clamp01(decay);
	SurfaceTone tone;
	tone.level = 0.26f + 0.30f * h;
	tone.seconds = 0.09f + 0.12f * (1.0f - d);
	tone.smallShare = 0.20f + 0.60f * h;
	tone.largeShare = 1.0f - 0.35f * h;
	tone.bed = 0.10f * (1.0f - h) + 0.06f * (1.0f - g) * (1.0f - d);
	return tone;
}
inline SurfaceTone NeutralSurfaceTone() {
	return SurfaceToneFrom(0.35f, 0.30f, 0.55f);
}
// Surfaces (beta) strength: how much of the measured tone replaces the neutral footstep, so the row is a level and
// not a switch; 0 leaves the tick exactly as the Footsteps row had it
inline SurfaceTone BlendSurfaceTone(const SurfaceTone& neutral, const SurfaceTone& surface, float amount) {
	const float t = Clamp01(amount);
	SurfaceTone tone;
	tone.level = neutral.level + (surface.level - neutral.level) * t;
	tone.seconds = neutral.seconds + (surface.seconds - neutral.seconds) * t;
	tone.largeShare = neutral.largeShare + (surface.largeShare - neutral.largeShare) * t;
	tone.smallShare = neutral.smallShare + (surface.smallShare - neutral.smallShare) * t;
	tone.bed = neutral.bed + (surface.bed - neutral.bed) * t;
	return tone;
}
inline float FootstepLevel(float speed, const SurfaceTone& tone) {
	return tone.level * (0.72f + 0.28f * Unit(speed, 6.0f));
}
}
