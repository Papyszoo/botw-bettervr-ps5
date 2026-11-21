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
inline constexpr float kGlideGustStrength = 0.42f; // fixed: the direct pulse path, not the row's level; 300-600 ms

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
// pulse). The director adds a little hysteresis so a life at exactly half does not flicker.
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
}
