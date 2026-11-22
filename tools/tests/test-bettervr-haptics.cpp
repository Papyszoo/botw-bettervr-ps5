// SPDX-License-Identifier: GPL-3.0-or-later
// The v0.8 haptic director's effect levels and rates (port/bettervr/haptics_ps5.h), and the audio band
// analysis its weather and surface layers read (port/bettervr/audio_bands.h), on a PC. The director itself
// reads the game on the PS5; these are the numbers its rows and timings promise.
#include "bettervr/audio_bands.h"
#include "bettervr/haptics_ps5.h"

#include <cmath>
#include <iostream>
#include <vector>

static int failures = 0;
static void Check(bool ok, const char* name)
{
	std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
	failures += !ok;
}

static float Distance(float a, float b)
{
	return a > b ? a - b : b - a;
}

int main()
{
	using namespace ps5hapt;

	Check(kShieldSurf == 0x101E0498 && kShieldSurfLanding == 0x101E0990 &&
	      kGlide == 0x101E06AC && kSwim == 0x101E08B0 && kClimb == 0x101E0390 && kLadder == 0x101E0658,
		"the effect states are the hashes the console logs hold");

	// Shield surf
	Check(Distance(SurfLevel(0.0f), 0.18f) < 1e-6f && Distance(SurfLevel(18.0f), 0.60f) < 1e-6f,
		"the surf bed runs 0.18 to 0.60 between still and 18 m/s");
	Check(SurfLevel(6.0f) < SurfLevel(12.0f) && SurfLevel(40.0f) <= 0.60f, "the surf bed rises with speed and stays in range");
	Check(Distance(SurfLandingLevel(0.0f), 0.25f) < 1e-6f && Distance(SurfLandingLevel(18.0f), 0.75f) < 1e-6f,
		"the surf landing runs 0.25 to 0.75 with the speed it had");
	for (int i = 0; i <= 40; i++)
	{
		const float level = SurfLandingLevel(i * 0.5f);
		if (level < 0.0f || level > 1.0f)
		{
			Check(false, "surf levels stay in 0..1");
			break;
		}
	}

	// Paraglider
	Check(Distance(GlideLevel(0.0f, 0.0f), 0.10f) < 1e-6f && GlideLevel(30.0f, 30.0f) <= 0.45f,
		"the wind bed starts at 0.10 and is capped at 0.45");
	Check(GlideLevel(10.0f, 0.0f) < GlideLevel(10.0f, 5.0f), "a dive adds to the wind bed");

	// Swim
	Check(Distance(SwimRushLevel(0.0f), 0.08f) < 1e-6f && Distance(SwimRushLevel(6.0f), 0.38f) < 1e-6f,
		"the swim rush runs 0.08 to 0.38 between still and 6 m/s");

	// Climb
	Check(Distance(ClimbStepHz(0.0f), 1.10f) < 1e-6f && Distance(ClimbStepHz(1.2f), 2.20f) < 1e-6f,
		"climbing runs 1.1 to 2.2 steps a second");
	Check(ClimbLevel(0.0f) < ClimbLevel(1.0f) && ClimbLevel(2.0f) <= 0.47f, "each climbing step gets stronger with the vertical speed");

	// Gallop
	Check(Distance(GallopHz(0.0f), 2.0f) < 1e-6f && Distance(GallopHz(12.0f), 4.0f) < 1e-6f && GallopHz(30.0f) <= 4.0f,
		"the gallop runs 2 to 4 Hz and stops at 4");
	Check(GallopLevel(0.0f) < GallopLevel(10.0f) && GallopLevel(30.0f) <= 0.60f, "the gallop is stronger with speed");

	// Low health
	Check(Distance(HeartPeriod(0.5f), 1.10f) < 1e-6f, "half life is one beat per 1.1 s");
	Check(Distance(HeartPeriod(0.0f), 0.55f) < 1e-6f, "near death is one beat per 0.55 s");
	Check(HeartPeriod(0.75f) == 0.0f && HeartPeriod(1.0f) == 0.0f, "above half life there is no beat");
	Check(HeartPeriod(0.25f) > HeartPeriod(0.1f), "the beat rate rises as life falls");
	Check(Distance(HeartHandLevel(0.5f), 0.30f) < 1e-6f && Distance(HeartHandLevel(0.0f), 0.65f) < 1e-6f,
		"the hands' beat runs 0.30 to 0.65");
	Check(Distance(HeartHeadLevel(0.5f), 0.70f) < 1e-6f && Distance(HeartHeadLevel(0.0f), 0.95f) < 1e-6f,
		"the headset's beat runs 0.70 to 0.95 before the limit");

	Check(Distance(BeatEnvelope(0.0f), 0.0f) < 1e-6f && Distance(BeatEnvelope(1.0f), 0.0f) < 1e-6f,
		"a beat starts and ends at nothing");
	Check(Distance(BeatEnvelope(0.18f), 1.0f) < 1e-6f && BeatEnvelope(0.5f) < 1.0f,
		"a beat peaks after its short attack and decays");
	Check(Distance(FallingEnvelope(0.0f), 1.0f) < 1e-6f && Distance(FallingEnvelope(1.0f), 0.0f) < 1e-6f,
		"a landing starts full and ends at nothing");

	// Every level stays in range over the range of speeds the game can show.
	bool inRange = true;
	for (int i = 0; i <= 60; i++)
	{
		const float value = i * 0.5f;
		inRange = inRange && SurfLevel(value) >= 0.0f && SurfLevel(value) <= 1.0f;
		inRange = inRange && GlideLevel(value, value) >= 0.0f && GlideLevel(value, value) <= 1.0f;
		inRange = inRange && SwimRushLevel(value) >= 0.0f && SwimRushLevel(value) <= 1.0f;
		inRange = inRange && ClimbLevel(value) >= 0.0f && ClimbLevel(value) <= 1.0f;
		inRange = inRange && GallopLevel(value) >= 0.0f && GallopLevel(value) <= 1.0f;
	}
	Check(inRange, "every effect level stays in 0..1 for 0-30 m/s");

	// Weather & Water
	{
		const WeatherMix quiet = WeatherFromBands(0.0f, 0.0f, 0.0f, 0.0f);
		Check(quiet.rain == 0.0f && quiet.water == 0.0f && quiet.wind == 0.0f, "silence detects no weather");

		const WeatherMix rain = WeatherFromBands(0.12f, 0.20f, 0.70f, 20.0f);
		Check(rain.rain > 0.50f && rain.rain > rain.water && rain.rain > rain.wind,
			"a bright band with many micro-onsets reads as rain");
		const WeatherMix water = WeatherFromBands(0.50f, 0.70f, 0.08f, 1.0f);
		Check(water.water > 0.30f && water.water > water.rain, "a steady mid and low band with few onsets reads as water");
		const WeatherMix wind = WeatherFromBands(0.75f, 0.25f, 0.05f, 1.0f);
		Check(wind.wind > 0.30f && wind.wind > wind.water, "a low swell with little high band reads as wind");
		Check(RainTickHz(0.0f) == 6.0f && RainTickHz(1.0f) == 30.0f && RainTickHz(2.0f) == 30.0f,
			"the rain patter runs 6 to 30 ticks a second and stays capped");
		const float tick = RainTickLevel(0.6f, 0.5f);
		Check(tick > 0.20f && tick < 0.80f, "one rain tap is a 20-80 % level before the row");
		Check(RainBedLevel(0.0f) < RainBedLevel(1.0f) && RainBedLevel(1.0f) <= 0.70f, "the rain bed rises and stays capped");
		Check(WaterBedLevel(0.0f) < WaterBedLevel(1.0f) && WaterBedLevel(1.0f) <= 0.78f,
			"the water bed rises and stays capped");
		Check(WindBedLevel(0.0f) < WindBedLevel(1.0f) && WindBedLevel(1.0f) <= 0.70f, "the wind bed rises and stays capped");

		// the console report's fix: ordinary music must not move a hand without the game state a bed stands on
		const WeatherMix music = WeatherFromBands(0.25f, 0.38f, 0.15f, 0.0f);
		Check(music.water > kWeatherBedOn && music.wind > kWeatherBedOn,
			"ordinary music still reads as water and wind in the analyser");
		Check(WeatherBedLevel(WeatherBed::Water, music.water, WeatherBedAllowed(WeatherBed::Water, false, false, false), false) == 0.0f &&
		      WeatherBedLevel(WeatherBed::Wind, music.wind, WeatherBedAllowed(WeatherBed::Wind, false, false, false), false) == 0.0f,
			"music-like bands with no water or glide state add no bed");
		Check(WeatherBedLevel(WeatherBed::Water, music.water, WeatherBedAllowed(WeatherBed::Water, true, false, false), false) > 0.0f,
			"the water bed is felt once swimming");
		Check(WeatherBedLevel(WeatherBed::Wind, music.wind, WeatherBedAllowed(WeatherBed::Wind, false, true, false), false) > 0.0f,
			"the wind bed is felt once gliding, falling or surfing");
		Check(WeatherBedLevel(WeatherBed::Rain, 1.0f, WeatherBedAllowed(WeatherBed::Rain, false, false, true), false) == 0.0f,
			"rain stays off while the game's weather has no cheap reader");
		Check(WeatherBedLevel(WeatherBed::Water, kWeatherBedOff + 0.01f, true, true) > 0.0f &&
		      WeatherBedLevel(WeatherBed::Water, kWeatherBedOff - 0.01f, true, true) == 0.0f,
			"a running bed holds to the release threshold and then lets go");
	}

	// Footsteps & Ground
	{
		Check(IsWalkState(0x101E064C) && IsWalkState(0x101E06D0) && IsWalkState(0x101E08C0) &&
		      IsWalkState(0x101E04A4) && !IsWalkState(kGlide) && !IsWalkState(kSwim) && !IsWalkState(0),
			"only the logged walk states take footsteps");
		Check(StepRateHz(0.0f) == 0.0f && StepRateHz(0.35f) == 0.0f, "standing still has no steps");
		Check(StepRateHz(1.4f) > 1.2f && StepRateHz(1.4f) < 2.2f, "a walk is about 1.4 steps a second");
		Check(StepRateHz(3.0f) < StepRateHz(5.0f) && StepRateHz(30.0f) <= 4.5f,
			"the cadence rises with speed and stops at 4.5 steps a second");
		Check(StepStride(0.0f) < StepStride(6.0f), "the stride grows with speed");
		Check(StepLevel(0.0f) < StepLevel(6.0f) && StepLevel(6.0f) <= 0.52f, "a faster step is stronger");

		const SurfaceTone hard = SurfaceToneFrom(0.90f, 0.80f, 0.90f);
		const SurfaceTone soft = SurfaceToneFrom(0.10f, 0.10f, 0.10f);
		Check(hard.level > soft.level, "a hard surface ticks louder");
		Check(hard.seconds < soft.seconds, "a hard surface's tick is shorter");
		Check(hard.smallShare > soft.smallShare, "hardness moves the pad's balance to the small motor");
		Check(soft.bed > hard.bed, "a soft surface leaves a bed and stone does not");
		Check(hard.seconds >= 0.09f && hard.seconds <= 0.21f && hard.smallShare <= 0.80f,
			"surface timings and motor shares stay in their ranges");
		Check(FootstepLevel(3.0f, NeutralSurfaceTone()) > 0.15f &&
		      FootstepLevel(3.0f, NeutralSurfaceTone()) < 0.40f, "the neutral footstep sits in a usable range");

		const SurfaceTone neutral = NeutralSurfaceTone();
		Check(Distance(BlendSurfaceTone(neutral, hard, 0.0f).level, neutral.level) < 1e-6f,
			"Surfaces at 0 % leaves the footstep's level alone");
		Check(Distance(BlendSurfaceTone(neutral, hard, 1.0f).level, hard.level) < 1e-6f,
			"Surfaces at 100 % reaches the measured surface's level");
		Check(BlendSurfaceTone(neutral, hard, 0.5f).level > neutral.level &&
		      BlendSurfaceTone(neutral, hard, 0.5f).level < hard.level,
			"Surfaces strength moves the footstep's level between neutral and the surface");
	}

	// The audio analyser: a low tone, a high tone, an onset train and one captured footstep.
	{
		using namespace ps5audio;
		constexpr uint32_t rate = 48000;
		constexpr uint32_t block = 240;
		std::vector<int16_t> samples(block * 2);
		ThreadAnalyzer(rate).Configure(rate);

		auto feedTone = [&](float hz, int blocks) {
			for (int b = 0; b < blocks; b++) {
				for (uint32_t i = 0; i < block; i++) {
					const float t = (float)(b * block + i) / (float)rate;
					const float v = 0.5f * std::sin(2.0f * kPi * hz * t);
					samples[i * 2] = (int16_t)std::lround(v * 32767.0f);
					samples[i * 2 + 1] = samples[i * 2];
				}
				Analyze(samples.data(), block, 2, rate);
			}
		};

		feedTone(60.0f, 200);
		Snapshot low = Shared().Now();
		Check(low.low > 0.30f && low.low > low.mid * 2.0f && low.low > low.high * 2.0f,
			"a 60 Hz tone lands in the low band");

		ThreadAnalyzer(rate).Configure(44100); // a new rate resets the filters
		ThreadAnalyzer(rate).Configure(rate);
		feedTone(4000.0f, 200);
		Snapshot high = Shared().Now();
		Check(high.high > 0.30f && high.high > high.low * 2.0f, "a 4 kHz tone lands in the high band");

		ThreadAnalyzer(rate).Configure(44100);
		ThreadAnalyzer(rate).Configure(rate);
		uint32_t seed = 12345;
		auto noise = [&]() {
			seed = seed * 1664525u + 1013904223u;
			return ((float)(seed >> 8) / 16777216.0f) * 2.0f - 1.0f;
		};
		for (int burst = 0; burst < 20; burst++) {
			for (uint32_t i = 0; i < block; i++) {
				const float v = 0.25f * noise();
				samples[i * 2] = (int16_t)std::lround(v * 32767.0f);
				samples[i * 2 + 1] = samples[i * 2];
			}
			Analyze(samples.data(), block, 2, rate);
			std::fill(samples.begin(), samples.end(), (int16_t)0);
			for (int quiet = 0; quiet < 3; quiet++)
				Analyze(samples.data(), block, 2, rate);
		}
		Check(Shared().Now().micro > 3.0f, "a train of short noise bursts raises the micro-onset rate");

		ThreadAnalyzer(rate).Configure(44100);
		ThreadAnalyzer(rate).Configure(rate);
		const uint32_t step = RequestStep();
		bool ready = false;
		for (int b = 0; b < 40 && !ready; b++) {
			for (uint32_t i = 0; i < block; i++) {
				const float thump = b < 8 ? 0.45f * std::sin(2.0f * kPi * 80.0f * (float)(b * block + i) / (float)rate) : 0.0f;
				const float grit = b < 4 ? 0.20f * noise() : 0.0f;
				const float v = thump + grit;
				samples[i * 2] = (int16_t)std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f);
				samples[i * 2 + 1] = samples[i * 2];
			}
			Analyze(samples.data(), block, 2, rate);
			ready = StepReady(step);
		}
		const StepResult result = Step(step);
		Check(ready && result.valid, "the footstep capture completes and is valid");
		Check(result.low > 0.0f && result.hardness >= 0.0f && result.hardness <= 1.0f &&
		      result.grain >= 0.0f && result.grain <= 1.0f && result.decay >= 0.0f && result.decay <= 1.0f,
			"the captured footstep reports bands and coordinates in range");
	}

	return failures ? 1 : 0;
}
