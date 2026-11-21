// SPDX-License-Identifier: GPL-3.0-or-later
// The v0.8 haptic director (docs/bettervr-ps5/STATUS.md, the v0.8 haptics entry): Shield Surf, Traversal and Low
// Health, synthesised on BetterVR's existing 60 Hz mixer thread (rendering/rumble.h) as one extra layer. It reads a
// few values the game already exposes (the player actor's matrix, the life field, the move flags and the
// PlayerNormal state the game thread publishes) and hands back per-hand and per-motor levels; one-shots that should
// be strong on the headset (a surf landing, a glide gust) go through the direct pulse path. No thread, no queue,
// no GPU work and no per-frame logging.
#include "pch.h"
#include "audio_bands.h"
#include "haptics_ps5.h"

#include "cemu/guest_memory.h"
#include "game/botw/game_state.h"
#include "game/botw/game_structs.h"
#include "game/botw/player/player.h"
#include "utils/mod_settings.h"

// the runtime's direct headset pulse (port/bettervr/xr/runtime.cpp): a 60 ms kick, then the level held for ms
extern "C" int BetterVR_PS5_HeadsetPulse(float strength, int ms);

namespace {
using Clock = std::chrono::steady_clock;

int64_t NowNs()
{
	return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
}

// The game thread's half: the PlayerNormal state and the shield's guard. The mixer thread reads these; everything
// below is one atomic load a tick.
std::atomic<uint32_t> s_stateHash{0};
std::atomic<uint32_t> s_pending{0};
std::atomic<bool> s_guarding{false};
std::atomic<int64_t> s_guardChangedNs{0};
std::atomic<int64_t> s_guardPressedNs{0};
constexpr uint32_t kPendingSurfLanding = 1;

// Link's life, in quarter hearts (the field the V3 console scan proved)
constexpr uint32_t kLifeOffset = 0x540;
constexpr int32_t kLifeMax = 480;

// Test request 76's tuning run: six 20 s weather stops plus travel and weather waiting at 2 Hz, about 140 steps
// on seven surfaces twice, and enough transitions for the later stops after music has flipped the weather lines
constexpr int kEnvLogCap = 1800;
constexpr int kWeatherLogCap = 240;
constexpr int kStepLogCap = 240;

bool HapticsLogging()
{
	return GetSettings().hapticsLogPatterns;
}

bool CanLog(int& logged, int cap = 24)
{
	if (!HapticsLogging() || logged >= cap)
		return false;
	++logged;
	return true;
}

// "hands+headset+pad" for the effects' lines, as player.cpp's damage line says it
std::string DescribeDevices(uint32_t devices)
{
	std::string text;
	if (devices & HapticDeviceSense)
		text = "hands";
	if (devices & HapticDeviceHeadset)
		text += text.empty() ? "headset" : "+headset";
	if (devices & HapticDevicePad)
		text += text.empty() ? "pad" : "+pad";
	return text.empty() ? "none" : text;
}

// The mixer reads a few fields after the game thread publishes the player. Check that the address is still the one
// the actor list named, and that the block still carries that actor's name, before and after the reads: a load can
// free the block and a new actor can take the address between two ticks.
bool IsCurrentPlayer(uint32_t player)
{
	if (player == 0 || Guest::s_baseAddress == 0 || Player::GetAddress() != player)
		return false;
	const auto name = Guest::Get<sead::FixedSafeString40>(player + offsetof(ActorWiiU, name));
	return std::strncmp(name.data, "GameROMPlayer", sizeof("GameROMPlayer")) == 0;
}

// v0.8's parry/flurry diagnosis: their PlayerNormal hashes are not in any console log, and the public decomp's
// action names (actionPlayerGuardJust, actionPlayerCutAfterJust) have no V208 hash map, so no state can be called a
// parry from here. Every state the game enters while the shield is up, and every state for two seconds after a
// dodge (a flurry follows a perfect one), is logged with its hash, once for a state never seen while guarding, and
// always inside a guard-press or dodge window. The cap keeps a session from filling the log. One run with a parry
// and a flurry then names them or shows that the game keeps one state through both.
void LogCombatProbe(uint32_t stateNamePtr)
{
	static bool s_armed = false;
	static int s_logged = 0;
	static int64_t s_lastDodgeNs = 0;
	static std::unordered_set<uint32_t> s_guardingHashes;
	const int64_t now = NowNs();
	const bool dodge = stateNamePtr == ps5hapt::kBackflip || stateNamePtr == ps5hapt::kSideStep;
	if (dodge)
		s_lastDodgeNs = now;

	if (!s_armed)
	{
		s_armed = true;
		if (HapticsLogging())
			Log::print<INFO>("[vib] combat probe: parry and flurry have no known PlayerNormal hash; a state in a guard or dodge window is logged, at most 24 lines");
	}
	if (!HapticsLogging() || s_logged >= 24)
		return;

	const bool guarding = s_guarding.load(std::memory_order_relaxed);
	const int64_t guardPressed = s_guardPressedNs.load(std::memory_order_relaxed);
	const int64_t guardChanged = s_guardChangedNs.load(std::memory_order_relaxed);
	const int64_t sinceGuardPress = guardPressed == 0 ? -1 : now - guardPressed;
	const int64_t sinceGuardChange = guardChanged == 0 ? -1 : now - guardChanged;
	const int64_t sinceDodge = s_lastDodgeNs == 0 ? -1 : now - s_lastDodgeNs;
	const bool afterDodge = sinceDodge >= 0 && sinceDodge < 2'000'000'000;
	const bool afterGuardPress = sinceGuardPress >= 0 && sinceGuardPress < 800'000'000;
	const bool firstWhileGuarding = guarding && s_guardingHashes.insert(stateNamePtr).second;
	if (!afterDodge && !afterGuardPress && !firstWhileGuarding)
		return;

	std::string when;
	if (afterDodge)
		when = fmt::format("{} ms after a dodge", sinceDodge / 1'000'000);
	else if (afterGuardPress)
		when = fmt::format("{} ms after the guard went up", sinceGuardPress / 1'000'000);
	else
		when = "while guarding";
	if (!guarding && !afterDodge && sinceGuardChange >= 0 && sinceGuardChange < 600'000'000)
		when = fmt::format("{} ms after the guard went down", sinceGuardChange / 1'000'000);

	Log::print<INFO>("[vib] combat probe: state '{}' (0x{:08X}) {}", Guest::CString(stateNamePtr), stateNamePtr, when);
	++s_logged;
}

// The mixer thread's half. One tick reads the player's position and life, compares the state, and builds the
// effects' levels. Speeds are smoothed over about 0.12 s so a game frame that did not advance does not drop a bed
// to zero for one tick.
class Director
{
public:
	void Frame(float* senseLeft, float* senseRight, float* padLarge, float* padSmall, float* headset)
	{
		*senseLeft = 0.0f;
		*senseRight = 0.0f;
		*padLarge = 0.0f;
		*padSmall = 0.0f;
		*headset = 0.0f;

		const int64_t nowNs = NowNs();
		const float dt = m_lastNs == 0 ? 0.0f : std::clamp((nowNs - m_lastNs) * 1.0e-9f, 0.0f, 0.1f);
		m_lastNs = nowNs;
		const double now = (double)nowNs * 1.0e-9;
		const uint32_t player = Player::GetAddress();
		const bool loading = Guest::s_baseAddress != 0 && Screens::IsLoadingVisible();
		if (loading)
		{
			// a load can put the new Link at the old actor's address: his life high-water is not the old one's
			m_player = 0;
			m_lifeMax = 0;
			m_life = -1;
			m_healthOn = false;
		}

		// a menu, a load, a swapped actor or no Link yet: no effect may linger into it
		if (!IsCurrentPlayer(player) || !GameState::IsInGame() || loading)
		{
			s_pending.store(0, std::memory_order_relaxed); // a landing while no effect runs is not a thump later
			if (m_running)
			{
				m_running = false;
				m_surfOn = false;
				m_mode = Mode::None;
				m_hasPos = false;
				m_speed = 0.0f;
				m_vertical = 0.0f;
				m_landingPending = false;
				m_landUntil = 0.0;
				m_splashUntil = 0.0;
				m_dripUntil = 0.0;
				m_rainNext = 0.0;
				m_rainTickUntil = 0.0;
				m_thunderUntil = 0.0;
				m_stepTick = -1.0;
				m_stepOn = false;
				m_stepPhase = 0.0f;
				m_thunderSeen = ps5audio::Shared().thunder.load(std::memory_order_relaxed);
				m_state = s_stateHash.load(std::memory_order_relaxed);
			}
			return;
		}

		if (player != m_player)
			ResetPlayer(player);
		else if (!m_running)
		{
			// back from a menu with the same Link: resync the one-shots, keep the life high-water and the beat
			m_state = s_stateHash.load(std::memory_order_relaxed);
			m_hasPos = false;
			m_speed = 0.0f;
			m_vertical = 0.0f;
			m_surfOn = false;
			m_mode = Mode::None;
			m_landingPending = false;
			m_landUntil = 0.0;
			m_splashUntil = 0.0;
			m_dripUntil = 0.0;
			m_rainNext = 0.0;
			m_rainTickUntil = 0.0;
			m_thunderUntil = 0.0;
			m_thunderSeen = ps5audio::Shared().thunder.load(std::memory_order_relaxed);
			m_stepTick = -1.0;
			m_stepOn = false;
			m_stepPhase = 0.0f;
			m_nextBeat = now;
			s_pending.store(0, std::memory_order_relaxed);
		}
		m_running = true;
		if (!m_armed)
		{
			m_armed = true;
			if (HapticsLogging())
				Log::print<INFO>("[vib] haptics: director on the 60 Hz mixer, per-hand Sense and two pad motors; Shield Surf, Traversal and Low Health on the Vibration page");
		}

		SamplePlayer(dt, player);
		m_riding = Player::IsRiding(true);
		m_lifeNow = Guest::Get<int32_t>(player + kLifeOffset).getLE();
		if (!IsCurrentPlayer(player))
		{
			// the actor changed under this tick: its matrix, life and flags are not one Link's
			m_hasPos = false;
			return;
		}
		m_state = s_stateHash.load(std::memory_order_relaxed);
		const uint32_t pending = s_pending.exchange(0, std::memory_order_acq_rel);
		if (pending & kPendingSurfLanding)
			m_landingPending = true;

		ModSettings& settings = GetSettings();
		UpdateSurf(now, dt, settings, senseLeft, senseRight, padLarge, padSmall);
		UpdateTraversal(now, dt, settings, senseLeft, senseRight, padLarge, padSmall);
		UpdateAmbience(now, dt, settings, senseLeft, senseRight, padLarge, padSmall);
		UpdateFootsteps(now, dt, settings, senseLeft, senseRight, padLarge, padSmall, headset);
		UpdateHealth(now, settings, senseLeft, senseRight, padLarge, padSmall, headset);
	}

private:
	enum class Mode : uint8_t { None, Glide, Swim, Climb, Gallop };

	void SamplePlayer(float dt, uint32_t player)
	{
		const BEMatrix34 mtx = Guest::Get<BEMatrix34>(player + offsetof(ActorWiiU, mtx));
		const glm::fvec3 pos{ mtx.pos_x.getLE(), mtx.pos_y.getLE(), mtx.pos_z.getLE() };
		if (!glm::all(glm::isfinite(pos)))
		{
			m_hasPos = false;
			return;
		}
		if (m_hasPos && dt > 0.0f)
		{
			const float dx = pos.x - m_pos.x;
			const float dy = pos.y - m_pos.y;
			const float dz = pos.z - m_pos.z;
			const float speed = std::min(std::sqrt(dx * dx + dz * dz) / dt, 60.0f);
			const float vertical = std::clamp(dy / dt, -30.0f, 30.0f);
			const float blend = 1.0f - std::exp(-dt / 0.12f);
			m_speed += (speed - m_speed) * blend;
			m_vertical += (vertical - m_vertical) * blend;
		}
		m_pos = pos;
		m_hasPos = true;
	}

	void ResetPlayer(uint32_t player)
	{
		m_player = player;
		m_state = s_stateHash.load(std::memory_order_relaxed);
		m_life = -1;
		m_lifeNow = -1;
		m_lifeMax = 0; // the fraction is against the highest life this actor has had
		m_healthOn = false;
		m_riding = false;
		m_nextBeat = 0.0;
		m_beatAt = 0.0;
		m_surfOn = false;
		m_surfEnter = 0.0;
		m_landingPending = false;
		m_landUntil = 0.0;
		m_landLevel = 0.0f;
		m_mode = Mode::None;
		m_climbPhase = 0.0f;
		m_gallopPhase = 0.0f;
		m_climbTick = -1.0;
		m_gallopTick = -1.0;
		m_splashUntil = 0.0;
		m_dripUntil = 0.0;
		m_gustAt = 0.0;
		m_rainNext = 0.0;
		m_rainTickUntil = 0.0;
		m_rainTickLevel = 0.0f;
		m_waterPhase = 0.0f;
		m_windPhase = 0.0f;
		m_thunderSeen = ps5audio::Shared().thunder.load(std::memory_order_relaxed);
		m_thunderUntil = 0.0;
		m_thunderLevel = 0.0f;
		m_lastThunderAt = -100.0;
		m_stepOn = false;
		m_stepPhase = 0.0f;
		m_stepHand = 0;
		m_stepTick = -1.0;
		m_stepGeneration = 0;
		m_surface = ps5hapt::NeutralSurfaceTone();
		m_stepTone = ps5hapt::NeutralSurfaceTone();
		m_surfaceHardness = 0.35f;
		m_surfaceGrain = 0.30f;
		m_surfaceDecay = 0.55f;
		m_hasPos = false;
		m_speed = 0.0f;
		m_vertical = 0.0f;
		s_pending.store(0, std::memory_order_relaxed);
	}

	void UpdateSurf(double now, float dt, ModSettings& settings, float* senseLeft, float* senseRight,
	                float* padLarge, float* padSmall)
	{
		const float gain = settings.GetHapticsGain(HapticCategory::ShieldSurf);
		const uint32_t devices = settings.GetHapticsDeviceMask(HapticCategory::ShieldSurf);
		const bool surfing = m_state == ps5hapt::kShieldSurf;

		if (surfing != m_surfOn)
		{
			m_surfOn = surfing;
			m_scrapePhase = 0.0f;
			if (surfing)
				m_surfEnter = now;
			if (surfing && CanLog(m_surfLogs))
				Log::print<INFO>("[vib] surf: bed on at {:.1f} m/s -> hands {:.2f}, pad large {:.2f} small {:.2f}, Shield Surf {}% [{}]",
				                 m_speed, ps5hapt::SurfLevel(m_speed) * gain, ps5hapt::SurfLevel(m_speed) * gain,
				                 ps5hapt::SurfLevel(m_speed) * gain * 0.50f,
				                 (unsigned)settings.HapticsStrength(HapticCategory::ShieldSurf).load(), DescribeDevices(devices));
			else if (!surfing && CanLog(m_surfLogs))
				Log::print<INFO>("[vib] surf: bed off after {:.1f} s, {:.1f} m/s", now - m_surfEnter, m_speed);
		}

		if (surfing && gain > 0.0f)
		{
			m_scrapePhase = std::fmod(m_scrapePhase + dt * 2.0f * glm::pi<float>() * 17.0f, 2.0f * glm::pi<float>());
			const float scrape = 0.82f + 0.18f * std::sin(m_scrapePhase);
			const float level = ps5hapt::SurfLevel(m_speed) * gain * scrape;
			if (devices & HapticDeviceSense)
			{
				*senseLeft = std::max(*senseLeft, level);
				*senseRight = std::max(*senseRight, level);
			}
			if (devices & HapticDevicePad)
			{
				*padLarge = std::max(*padLarge, level);
				*padSmall = std::max(*padSmall, level * 0.50f);
			}
		}

		if (m_landingPending)
		{
			m_landingPending = false;
			if (gain > 0.0f)
			{
				m_landUntil = now + ps5hapt::kSurfLandingSeconds;
				m_landLevel = ps5hapt::SurfLandingLevel(m_speed) * gain;
				const bool headset = (devices & HapticDeviceHeadset) != 0;
				if (headset && m_speed >= ps5hapt::kSurfLandingHeadsetSpeed)
				{
					const int hz = BetterVR_PS5_HeadsetPulse(0.45f * settings.GetHapticsHeadsetLimit(), 140);
					if (CanLog(m_surfLogs))
						Log::print<INFO>("[vib] surf: landing at {:.1f} m/s -> hands {:.2f}, pad large {:.2f} small {:.2f}, headset {} Hz for 140 ms",
						                 m_speed, m_landLevel, m_landLevel, m_landLevel * 0.60f, hz);
				}
				else if (CanLog(m_surfLogs))
					Log::print<INFO>("[vib] surf: landing at {:.1f} m/s -> hands {:.2f}, pad large {:.2f} small {:.2f}, headset {}",
					                 m_speed, m_landLevel, m_landLevel, m_landLevel * 0.60f,
					                 headset ? "off, a small drop" : "off, Shield Surf's Headset tick is off");
			}
		}

		if (now < m_landUntil)
		{
			const float progress = 1.0f - (float)((m_landUntil - now) / ps5hapt::kSurfLandingSeconds);
			const float level = m_landLevel * ps5hapt::FallingEnvelope(progress);
			if (devices & HapticDeviceSense)
			{
				*senseLeft = std::max(*senseLeft, level);
				*senseRight = std::max(*senseRight, level);
			}
			if (devices & HapticDevicePad)
			{
				*padLarge = std::max(*padLarge, level);
				*padSmall = std::max(*padSmall, level * 0.60f);
			}
		}
	}

	void UpdateTraversal(double now, float dt, ModSettings& settings, float* senseLeft, float* senseRight,
	                     float* padLarge, float* padSmall)
	{
		const float gain = settings.GetHapticsGain(HapticCategory::Traversal);
		const uint32_t devices = settings.GetHapticsDeviceMask(HapticCategory::Traversal);

		Mode mode = Mode::None;
		if (m_state == ps5hapt::kGlide)
			mode = Mode::Glide;
		else if (m_state == ps5hapt::kSwim)
			mode = Mode::Swim;
		else if (m_state == ps5hapt::kClimb || m_state == ps5hapt::kLadder)
			mode = Mode::Climb;
		else if (m_riding)
			mode = Mode::Gallop;

		if (mode != m_mode)
		{
			const Mode previous = m_mode;
			m_mode = mode;
			m_climbPhase = 0.0f;
			m_gallopPhase = 0.0f;
			m_climbTick = -1.0;
			m_gallopTick = -1.0;
			if (gain > 0.0f)
			{
				if (previous == Mode::Swim)
				{
					m_dripUntil = now + ps5hapt::kDripSeconds;
					m_dripLevel = ps5hapt::kDripLevel * gain;
					if (CanLog(m_traversalLogs))
						Log::print<INFO>("[vib] traversal: swim off, drip");
				}
				if (mode == Mode::Swim)
				{
					m_splashUntil = now + ps5hapt::kSplashSeconds;
					m_splashLevel = ps5hapt::kSplashLevel * gain;
					if (CanLog(m_traversalLogs))
						Log::print<INFO>("[vib] traversal: swim on, splash");
				}
				else if (mode == Mode::Glide)
				{
					m_gustAt = now + 6.0 + 6.0 * Rand01();
					if (CanLog(m_traversalLogs))
						Log::print<INFO>("[vib] traversal: paraglider on at {:.1f} m/s, descent {:.1f} m/s", m_speed, std::max(0.0f, -m_vertical));
				}
				else if (mode == Mode::Climb)
				{
					if (CanLog(m_traversalLogs))
						Log::print<INFO>("[vib] traversal: climb on at {:.2f} m/s ({:.2f} steps/s)", m_vertical, ps5hapt::ClimbStepHz(std::max(0.0f, m_vertical)));
				}
				else if (mode == Mode::Gallop)
				{
					if (CanLog(m_traversalLogs))
						Log::print<INFO>("[vib] traversal: gallop on at {:.1f} m/s ({:.2f} Hz)", m_speed, ps5hapt::GallopHz(m_speed));
				}
				else if (previous != Mode::None && CanLog(m_traversalLogs))
					Log::print<INFO>("[vib] traversal: off");
			}
		}

		if (gain <= 0.0f)
			return;

		// the splash and the drip outlive the state that started them
		if (now < m_splashUntil)
			AddBoth(m_splashLevel * ps5hapt::FallingEnvelope(1.0f - (float)((m_splashUntil - now) / ps5hapt::kSplashSeconds)),
			        devices, senseLeft, senseRight, padLarge, padSmall, 1.0f, 0.50f);
		if (now < m_dripUntil)
			AddBoth(m_dripLevel * ps5hapt::FallingEnvelope(1.0f - (float)((m_dripUntil - now) / ps5hapt::kDripSeconds)),
			        devices, senseLeft, senseRight, padLarge, padSmall, 1.0f, 0.50f);

		switch (mode)
		{
		case Mode::Glide:
		{
			m_swellPhase = std::fmod(m_swellPhase + dt * 2.0f * glm::pi<float>() * 0.7f, 2.0f * glm::pi<float>());
			const float level = ps5hapt::GlideLevel(m_speed, std::max(0.0f, -m_vertical)) * gain * (0.85f + 0.15f * std::sin(m_swellPhase));
			AddBoth(level, devices, senseLeft, senseRight, padLarge, padSmall, 1.0f, 0.45f);
			if ((devices & HapticDeviceHeadset) && now >= m_gustAt)
			{
				const int ms = 300 + (int)(301.0f * Rand01());
				const int hz = BetterVR_PS5_HeadsetPulse(ps5hapt::kGlideGustStrength * settings.GetHapticsHeadsetLimit(), ms);
				if (CanLog(m_traversalLogs))
					Log::print<INFO>("[vib] traversal: paraglider gust {} Hz for {} ms", hz, ms);
				m_gustAt = now + 6.0 + 6.0 * Rand01();
			}
			break;
		}
		case Mode::Swim:
		{
			m_swellPhase = std::fmod(m_swellPhase + dt * 2.0f * glm::pi<float>() * 1.2f, 2.0f * glm::pi<float>());
			const float level = ps5hapt::SwimRushLevel(m_speed) * gain * (0.86f + 0.14f * std::sin(m_swellPhase));
			AddBoth(level, devices, senseLeft, senseRight, padLarge, padSmall, 1.0f, 0.45f);
			break;
		}
		case Mode::Climb:
		{
			if (m_vertical > 0.05f)
			{
				m_climbPhase += dt * ps5hapt::ClimbStepHz(m_vertical);
				if (m_climbPhase >= 1.0f)
				{
					m_climbPhase = std::fmod(m_climbPhase, 1.0f);
					m_climbHand ^= 1;
					m_climbTick = now;
					m_climbLevel = ps5hapt::ClimbLevel(m_vertical) * gain;
				}
			}
			if (m_climbTick >= 0.0 && now - m_climbTick < ps5hapt::kClimbStepSeconds)
			{
				const float progress = (float)((now - m_climbTick) / ps5hapt::kClimbStepSeconds);
				const float level = m_climbLevel * ps5hapt::FallingEnvelope(progress);
				if (devices & HapticDeviceSense)
					*(m_climbHand == 0 ? senseLeft : senseRight) = std::max(*(m_climbHand == 0 ? senseLeft : senseRight), level);
				AddPad(level, devices, padLarge, padSmall, 1.0f, 0.60f);
			}
			break;
		}
		case Mode::Gallop:
		{
			if (m_speed > 0.8f)
			{
				m_gallopPhase += dt * ps5hapt::GallopHz(m_speed);
				if (m_gallopPhase >= 1.0f)
				{
					m_gallopPhase = std::fmod(m_gallopPhase, 1.0f);
					m_gallopHand ^= 1;
					m_gallopTick = now;
					m_gallopLevel = ps5hapt::GallopLevel(m_speed) * gain;
				}
			}
			if (m_gallopTick >= 0.0 && now - m_gallopTick < ps5hapt::kGallopStepSeconds)
			{
				const float progress = (float)((now - m_gallopTick) / ps5hapt::kGallopStepSeconds);
				const float level = m_gallopLevel * ps5hapt::FallingEnvelope(progress);
				if (devices & HapticDeviceSense)
					*(m_gallopHand == 0 ? senseLeft : senseRight) = std::max(*(m_gallopHand == 0 ? senseLeft : senseRight), level);
				AddPad(level, devices, padLarge, padSmall, 1.0f, 0.60f);
			}
			break;
		}
		case Mode::None:
		default:
			break;
		}
	}

	// Weather & Water (idea 6): the audio detector's three bands become a rain tick train, a smoother water bed
	// and a slow wind swell. Thunder is a low transient during rain or water and goes through the direct headset
	// pulse only when the row's Headset tick is on. The bands are logged at 2 Hz so one console run can tune them.
	void UpdateAmbience(double now, float dt, ModSettings& settings, float* senseLeft, float* senseRight,
	                    float* padLarge, float* padSmall)
	{
		const ps5audio::Snapshot audio = ps5audio::Shared().Now();
		const ps5hapt::WeatherMix mix = ps5hapt::WeatherFromBands(audio.low, audio.mid, audio.high, audio.micro);
		const float gain = settings.GetHapticsGain(HapticCategory::WeatherWater);
		const uint32_t devices = settings.GetHapticsDeviceMask(HapticCategory::WeatherWater);

		if (HapticsLogging() && now >= m_envLogAt && m_envLogs < kEnvLogCap)
		{
			m_envLogAt = now + 0.5;
			++m_envLogs;
			const std::string row = gain > 0.0f
			                            ? fmt::format("Weather & Water {}% [{}]",
			                                          (unsigned)settings.HapticsStrength(HapticCategory::WeatherWater).load(),
			                                          DescribeDevices(devices))
			                            : std::string("Weather & Water off");
			Log::print<INFO>("[vib] env: low {:.2f} mid {:.2f} high {:.2f}, onsets {:.1f}/s micro {:.1f}/s -> "
			                 "rain {:.2f} water {:.2f} wind {:.2f}, {}",
			                 audio.low, audio.mid, audio.high, audio.onsets, audio.micro, mix.rain, mix.water, mix.wind, row);
		}

		auto transition = [&](bool& on, bool next, const char* name, float level) {
			if (on == next)
				return;
			on = next;
			if (CanLog(m_weatherLogs, kWeatherLogCap))
				Log::print<INFO>("[vib] weather: {} {} at {:.2f}, bands low {:.2f} mid {:.2f} high {:.2f}, micro {:.1f}/s",
				                 name, next ? "on" : "off", level, audio.low, audio.mid, audio.high, audio.micro);
		};
		transition(m_rainOn, mix.rain > 0.20f, "rain", mix.rain);
		transition(m_waterOn, mix.water > 0.20f, "water", mix.water);
		transition(m_windOn, mix.wind > 0.20f, "wind", mix.wind);

		const uint32_t thunder = audio.thunder;
		if (m_thunderSeen == 0)
			m_thunderSeen = thunder; // a clap from before this Link is not replayed on him
		else if (thunder != m_thunderSeen)
		{
			m_thunderSeen = thunder;
			const bool stormy = mix.rain > 0.20f || mix.water > 0.30f;
			if (gain > 0.0f && devices != 0 && stormy && now >= m_lastThunderAt + ps5hapt::kThunderMinSeconds)
			{
				m_lastThunderAt = now;
				m_thunderUntil = now + ps5hapt::kThunderHandSeconds;
				m_thunderLevel = ps5hapt::kThunderHandLevel * gain;
				if (devices & HapticDeviceHeadset)
				{
					const int hz = BetterVR_PS5_HeadsetPulse(
					    ps5hapt::kThunderHeadsetStrength * settings.GetHapticsHeadsetLimit(), ps5hapt::kThunderHeadsetMs);
					if (CanLog(m_weatherLogs, kWeatherLogCap))
						Log::print<INFO>("[vib] thunder: hands {:.2f} for {:.0f} ms, headset {} Hz for {} ms [{}]",
						                 m_thunderLevel, ps5hapt::kThunderHandSeconds * 1000.0f, hz,
						                 ps5hapt::kThunderHeadsetMs, DescribeDevices(devices));
				}
				else if (CanLog(m_weatherLogs, kWeatherLogCap))
					Log::print<INFO>("[vib] thunder: hands {:.2f}, headset off (Weather & Water's Headset tick)",
					                 m_thunderLevel);
			}
		}

		if (gain <= 0.0f || devices == 0)
		{
			m_rainTickUntil = 0.0;
			m_thunderUntil = 0.0;
			return;
		}

		// the rain bed and its randomised taps; at most one tap a mixer tick, never a queue event
		if (mix.rain > 0.05f)
		{
			if (now >= m_rainNext)
			{
				const float chance = std::clamp(ps5hapt::RainTickHz(mix.rain) / 60.0f, 0.0f, 1.0f);
				if (Rand01() < chance)
				{
					m_rainTickUntil = now + ps5hapt::kRainTickSeconds;
					m_rainTickLevel = ps5hapt::RainTickLevel(mix.rain, Rand01()) * gain;
				}
				m_rainNext = now + 1.0 / 60.0;
			}
			AddBoth(ps5hapt::RainBedLevel(mix.rain) * gain, devices, senseLeft, senseRight, padLarge, padSmall, 0.35f,
			        1.0f);
		}
		else
			m_rainNext = 0.0;

		m_waterPhase = std::fmod(m_waterPhase + dt * 2.0f * glm::pi<float>() * 0.60f, 2.0f * glm::pi<float>());
		m_windPhase = std::fmod(m_windPhase + dt * 2.0f * glm::pi<float>() * 0.22f, 2.0f * glm::pi<float>());
		if (mix.water > 0.05f)
			AddBoth(ps5hapt::WaterBedLevel(mix.water) * gain * (0.88f + 0.12f * std::sin(m_waterPhase)), devices,
			        senseLeft, senseRight, padLarge, padSmall, 1.0f, 0.25f);
		if (mix.wind > 0.05f)
			AddBoth(ps5hapt::WindBedLevel(mix.wind) * gain * (0.86f + 0.14f * std::sin(m_windPhase)), devices,
			        senseLeft, senseRight, padLarge, padSmall, 1.0f, 0.10f);

		if (now < m_rainTickUntil)
		{
			const float progress = 1.0f - (float)((m_rainTickUntil - now) / ps5hapt::kRainTickSeconds);
			AddBoth(m_rainTickLevel * ps5hapt::FallingEnvelope(progress), devices, senseLeft, senseRight, padLarge,
			        padSmall, 0.35f, 1.0f);
		}
		if (now < m_thunderUntil)
		{
			const float progress = 1.0f - (float)((m_thunderUntil - now) / ps5hapt::kThunderHandSeconds);
			AddBoth(m_thunderLevel * ps5hapt::FallingEnvelope(progress), devices, senseLeft, senseRight, padLarge,
			        padSmall, 0.90f, 0.20f);
		}
	}

	// Footsteps & Ground (idea 7): a step per stride from the distance walked, alternating hands. The material
	// layer (Surfaces, beta) asks the audio thread for the 160 ms after the step and colours the next ticks with
	// the transient's hardness, grain and decay. No event is queued per step.
	void UpdateFootsteps(double now, float dt, ModSettings& settings, float* senseLeft, float* senseRight,
	                     float* padLarge, float* padSmall, float* headset)
	{
		const float gain = settings.GetHapticsGain(HapticCategory::FootstepsGround);
		const uint32_t devices = settings.GetHapticsDeviceMask(HapticCategory::FootstepsGround);
		const float surfaceGain = settings.GetHapticsGain(HapticCategory::SurfacesBeta);
		const uint32_t surfaceDevices = settings.GetHapticsDeviceMask(HapticCategory::SurfacesBeta);
		const bool surfaces = surfaceGain > 0.0f && surfaceDevices != 0;
		const ps5hapt::SurfaceTone neutral = ps5hapt::NeutralSurfaceTone();

		if (m_stepGeneration != 0 && ps5audio::StepReady(m_stepGeneration))
		{
			const ps5audio::StepResult result = ps5audio::Step(m_stepGeneration);
			m_stepGeneration = 0;
			if (result.valid)
			{
				const float hardness = result.hardness;
				const float grain = result.grain;
				const float decay = result.decay;
				const bool changed = std::abs(hardness - m_surfaceHardness) > 0.08f ||
				                     std::abs(grain - m_surfaceGrain) > 0.10f ||
				                     std::abs(decay - m_surfaceDecay) > 0.10f;
				m_surface = ps5hapt::SurfaceToneFrom(hardness, grain, decay);
				m_surfaceHardness = hardness;
				m_surfaceGrain = grain;
				m_surfaceDecay = decay;
				if ((changed || m_surfaceLogs < 3) && CanLog(m_surfaceLogs, 60))
					Log::print<INFO>("[vib] surface: low {:.2f} mid {:.2f} high {:.2f} -> hardness {:.2f} grain {:.2f} "
					                 "decay {:.2f}; tick {:.2f} for {:.2f} s, pad large {:.2f} small {:.2f}",
					                 result.low, result.mid, result.high, hardness, grain, decay, m_surface.level,
					                 m_surface.seconds, m_surface.largeShare, m_surface.smallShare);
			}
		}

		// the row's strength is how much of the measured tone replaces the neutral footstep, not a switch: at 0 %
		// the tick is the neutral one, at 100 % the full surface, and the Footsteps row keeps its own devices
		const ps5hapt::SurfaceTone surfaceTone =
		    surfaces ? ps5hapt::BlendSurfaceTone(neutral, m_surface, surfaceGain) : neutral;
		const uint32_t tickDevices = devices | (surfaces ? surfaceDevices : 0u);

		const bool walking = gain > 0.0f && devices != 0 && ps5hapt::IsWalkState(m_state) && !m_riding &&
		                     std::abs(m_vertical) < 1.2f && m_speed > 0.35f;
		if (!walking)
		{
			if (m_stepOn)
			{
				m_stepOn = false;
				m_stepPhase = 0.0f;
				m_stepTick = -1.0;
				if (CanLog(m_stepLogs, kStepLogCap))
					Log::print<INFO>("[vib] step: off");
			}
			return;
		}
		if (!m_stepOn)
		{
			m_stepOn = true;
			m_stepPhase = 0.0f;
			m_stepHand = 0;
			m_stepTick = -1.0;
			if (CanLog(m_stepLogs, kStepLogCap))
				Log::print<INFO>("[vib] step: on at {:.1f} m/s, {:.2f} steps/s, Surfaces (beta) {}", m_speed,
				                 ps5hapt::StepRateHz(m_speed), surfaces ? "on" : "off");
		}

		m_stepPhase += m_speed * dt / ps5hapt::StepStride(m_speed);
		if (m_stepPhase >= 1.0f)
		{
			m_stepPhase = std::fmod(m_stepPhase, 1.0f);
			m_stepHand ^= 1;
			m_stepTick = now;
			m_stepTone = surfaceTone;
			m_stepSeconds = m_stepTone.seconds;
			m_stepTickLevel = ps5hapt::FootstepLevel(m_speed, m_stepTone) * gain;
			if (surfaces)
				m_stepGeneration = ps5audio::RequestStep();
			if (CanLog(m_stepLogs, kStepLogCap))
			{
				const ps5audio::Snapshot audio = ps5audio::Shared().Now();
				Log::print<INFO>("[vib] step: {:.1f} m/s, {:.2f} steps/s, hand {}, bands low {:.2f} mid {:.2f} high "
				                 "{:.2f}; surface hardness {:.2f} grain {:.2f} decay {:.2f} -> {:.2f} for {:.2f} s, "
				                 "Surfaces (beta) {}",
				                 m_speed, ps5hapt::StepRateHz(m_speed), m_stepHand == 0 ? "left" : "right", audio.low,
				                 audio.mid, audio.high, m_surfaceHardness, m_surfaceGrain, m_surfaceDecay, m_stepTickLevel,
				                 m_stepSeconds, surfaces ? "on" : "off");
			}
		}

		if (m_stepTick >= 0.0 && now - m_stepTick < m_stepSeconds)
		{
			const float progress = (float)((now - m_stepTick) / m_stepSeconds);
			const float level = m_stepTickLevel * ps5hapt::FallingEnvelope(progress);
			if (tickDevices & HapticDeviceSense)
				*(m_stepHand == 0 ? senseLeft : senseRight) =
				    std::max(*(m_stepHand == 0 ? senseLeft : senseRight), level);
			AddPad(level, tickDevices, padLarge, padSmall, m_stepTone.largeShare, m_stepTone.smallShare);
			if (tickDevices & HapticDeviceHeadset)
				*headset = std::max(*headset, level);
		}
		// a soft surface leaves a bed under the ticks (grass, snow, ice); stone's is nearly zero
		if (surfaces && m_surface.bed > 0.0f)
			AddBoth(m_surface.bed * gain * surfaceGain * 0.60f, surfaceDevices, senseLeft, senseRight, padLarge,
			        padSmall, 0.80f, 0.20f);
	}

	void UpdateHealth(double now, ModSettings& settings, float* senseLeft, float* senseRight, float* padLarge,
	                  float* padSmall, float* headset)
	{
		if (m_player == 0 || m_lifeNow < 0 || m_lifeNow > kLifeMax)
			return; // a load's or a menu's value: no beat, and the high-water is kept
		m_life = m_lifeNow;
		m_lifeMax = std::max(m_lifeMax, m_life);

		const float gain = settings.GetHapticsGain(HapticCategory::LowHealth);
		const uint32_t devices = settings.GetHapticsDeviceMask(HapticCategory::LowHealth);
		const bool lives = m_life > 0 && m_lifeMax >= 4;
		const float fraction = lives ? (float)m_life / (float)m_lifeMax : 1.0f;
		// half starts the beat; above half there is none (the period is already zero there)
		if (!m_healthOn && lives && gain > 0.0f && devices != 0 && fraction <= ps5hapt::kHeartLifeHalf)
		{
			m_healthOn = true;
			m_nextBeat = now;
			m_beatAt = 0.0;
			if (CanLog(m_healthLogs, 12))
				Log::print<INFO>("[vib] health: heartbeat on, {} of {} quarter hearts seen ({:.0f}%), {:.2f} s a beat, hands {:.2f}, headset {:.2f} before the Headset Limit [{}]",
				                 m_life, m_lifeMax, fraction * 100.0f, ps5hapt::HeartPeriod(fraction),
				                 ps5hapt::HeartHandLevel(fraction) * gain, ps5hapt::HeartHeadLevel(fraction) * gain,
				                 DescribeDevices(devices));
		}
		else if (m_healthOn && (!lives || gain <= 0.0f || devices == 0 || fraction > ps5hapt::kHeartLifeHalf))
		{
			m_healthOn = false;
			if (CanLog(m_healthLogs, 12))
				Log::print<INFO>("[vib] health: heartbeat off at {} of {} quarter hearts seen ({:.0f}%)", m_life, m_lifeMax, fraction * 100.0f);
		}
		if (!m_healthOn)
			return;

		const float period = ps5hapt::HeartPeriod(fraction);
		m_nextBeat = std::min(m_nextBeat, m_beatAt + period); // a life falling mid-interval beats sooner
		if (now >= m_nextBeat)
		{
			m_beatAt = now;
			m_nextBeat = m_beatAt + period;
		}
		const float progress = (float)((now - m_beatAt) / ps5hapt::kBeatSeconds);
		if (progress >= 1.0f)
			return;
		const float envelope = ps5hapt::BeatEnvelope(progress);
		const float hands = ps5hapt::HeartHandLevel(fraction) * gain * envelope;
		if (devices & HapticDeviceSense)
		{
			*senseLeft = std::max(*senseLeft, hands);
			*senseRight = std::max(*senseRight, hands);
		}
		if (devices & HapticDevicePad)
		{
			*padLarge = std::max(*padLarge, hands);
			*padSmall = std::max(*padSmall, hands * 0.60f);
		}
		if (devices & HapticDeviceHeadset)
			*headset = std::max(*headset, ps5hapt::HeartHeadLevel(fraction) * gain * envelope);
	}

	// a bed or a one-shot for both hands, and the pad's two motors when the row has it; the shares are the
	// level each motor takes, so rain can live on the small motor and water on the large one
	void AddBoth(float level, uint32_t devices, float* senseLeft, float* senseRight, float* padLarge, float* padSmall,
	             float padLargeShare, float padSmallShare)
	{
		if (level <= 0.0f)
			return;
		if (devices & HapticDeviceSense)
		{
			*senseLeft = std::max(*senseLeft, level);
			*senseRight = std::max(*senseRight, level);
		}
		AddPad(level, devices, padLarge, padSmall, padLargeShare, padSmallShare);
	}

	void AddPad(float level, uint32_t devices, float* padLarge, float* padSmall, float padLargeShare,
	            float padSmallShare)
	{
		if (level <= 0.0f || (devices & HapticDevicePad) == 0)
			return;
		*padLarge = std::max(*padLarge, level * padLargeShare);
		*padSmall = std::max(*padSmall, level * padSmallShare);
	}

	float Rand01()
	{
		m_random = m_random * 1664525u + 1013904223u;
		return (float)(m_random >> 8) / 16777216.0f;
	}

	uint32_t m_player = 0;
	uint32_t m_state = 0;
	bool m_running = false;
	bool m_armed = false;
	int64_t m_lastNs = 0;
	bool m_hasPos = false;
	glm::fvec3 m_pos = {};
	float m_speed = 0.0f;
	float m_vertical = 0.0f;
	bool m_riding = false;

	bool m_surfOn = false;
	double m_surfEnter = 0.0;
	bool m_landingPending = false;
	double m_landUntil = 0.0;
	float m_landLevel = 0.0f;
	float m_scrapePhase = 0.0f;
	int m_surfLogs = 0;

	Mode m_mode = Mode::None;
	float m_climbPhase = 0.0f;
	float m_gallopPhase = 0.0f;
	float m_climbLevel = 0.0f;
	float m_gallopLevel = 0.0f;
	double m_climbTick = -1.0;
	double m_gallopTick = -1.0;
	int m_climbHand = 0;
	int m_gallopHand = 0;
	double m_splashUntil = 0.0;
	double m_dripUntil = 0.0;
	float m_splashLevel = 0.0f;
	float m_dripLevel = 0.0f;
	double m_gustAt = 0.0;
	float m_swellPhase = 0.0f;
	int m_traversalLogs = 0;

	bool m_rainOn = false;
	bool m_waterOn = false;
	bool m_windOn = false;
	double m_rainNext = 0.0;
	double m_rainTickUntil = 0.0;
	float m_rainTickLevel = 0.0f;
	float m_waterPhase = 0.0f;
	float m_windPhase = 0.0f;
	uint32_t m_thunderSeen = 0;
	double m_thunderUntil = 0.0;
	float m_thunderLevel = 0.0f;
	double m_lastThunderAt = -100.0;
	double m_envLogAt = 0.0;
	int m_envLogs = 0;
	int m_weatherLogs = 0;

	bool m_stepOn = false;
	float m_stepPhase = 0.0f;
	int m_stepHand = 0;
	double m_stepTick = -1.0;
	float m_stepTickLevel = 0.0f;
	float m_stepSeconds = 0.0f;
	uint32_t m_stepGeneration = 0;
	ps5hapt::SurfaceTone m_surface = ps5hapt::NeutralSurfaceTone();
	ps5hapt::SurfaceTone m_stepTone = ps5hapt::NeutralSurfaceTone();
	float m_surfaceHardness = 0.35f;
	float m_surfaceGrain = 0.30f;
	float m_surfaceDecay = 0.55f;
	int m_stepLogs = 0;
	int m_surfaceLogs = 0;

	int32_t m_life = -1;
	int32_t m_lifeNow = -1;
	int32_t m_lifeMax = 0;
	bool m_healthOn = false;
	double m_nextBeat = 0.0;
	double m_beatAt = 0.0;
	int m_healthLogs = 0;

	uint32_t m_random = 0x5EED1234u;
};
}

// the game thread (game/botw/player/player.cpp): every PlayerNormal state change
extern "C" void BetterVR_PS5_PlayerNormalState(uint32_t stateNamePtr)
{
	if (stateNamePtr == 0)
		return;
	s_stateHash.store(stateNamePtr, std::memory_order_relaxed);
	if (stateNamePtr == ps5hapt::kShieldSurfLanding)
		s_pending.fetch_or(kPendingSurfLanding, std::memory_order_release);
	LogCombatProbe(stateNamePtr);
}

// the game thread (game/botw/input/controls.cpp): the shield's guard, for the probe's window
extern "C" void BetterVR_PS5_GuardState(int guarding)
{
	const bool now = guarding != 0;
	if (s_guarding.exchange(now, std::memory_order_relaxed) == now)
		return;
	const int64_t changed = NowNs();
	s_guardChangedNs.store(changed, std::memory_order_relaxed);
	if (now)
		s_guardPressedNs.store(changed, std::memory_order_relaxed);
}

// the mixer thread (rendering/rumble.h, mix()): one layer of per-hand and per-motor levels a tick
extern "C" void BetterVR_PS5_HapticsEnvironment(float* senseLeft, float* senseRight, float* padLarge, float* padSmall,
                                               float* headset)
{
	if (senseLeft == nullptr || senseRight == nullptr || padLarge == nullptr || padSmall == nullptr || headset == nullptr)
		return;
	static Director s_director;
	s_director.Frame(senseLeft, senseRight, padLarge, padSmall, headset);
}
