// SPDX-License-Identifier: GPL-3.0-or-later
// The v0.8 haptic director's effect levels and rates (port/bettervr/haptics_ps5.h), on a PC. The director itself
// reads the game on the PS5; these are the numbers its rows and timings promise.
#include "bettervr/haptics_ps5.h"

#include <iostream>

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

	return failures ? 1 : 0;
}
