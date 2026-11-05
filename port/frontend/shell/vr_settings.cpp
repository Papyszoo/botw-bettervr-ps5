// SPDX-License-Identifier: GPL-3.0-or-later
// BotW VR (docs/bettervr-ps5): Settings > VR, BetterVR's options without its in-game menu or a file
// to edit. BetterVR keeps them in its ImGui ini (bettervr_settings.ini in the launcher's folder,
// patches/bettervr: BETTERVR_SETTINGS_INI), section [BetterVR][Settings], one Key=VALUE a line
// (hooking/settings.cpp; the keys and their defaults are utils/mod_settings.h's), and reads it as
// VR starts. The app's own options are in bettervr.json: "vr" (Play in VR or on TV), "bettervr"
// (false: the flat screen in the headset) and the Sense controllers' gripPitch and aimPitch
// (port/bettervr/xr/runtime.cpp). Both files are written at each change; the rest of each file
// (ImGui's other sections, other keys) is kept, and bettervr.json is read again before a write so
// the switches added outside the app (graphicsPack, reproject) survive Play in VR/TV and Settings > VR.

#include "shell_internal.h"
#include "../../app/paths.h"

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace ps5shell
{
	namespace
	{
		constexpr const char* kSection = "[BetterVR][Settings]";

		// BetterVR's choices: the values its ini takes, in the rows' order
		constexpr const char* kCamera[] = {"FIRST_PERSON", "THIRD_PERSON"};
		constexpr const char* kCutscenes[] = {"FOLLOW_DEFAULT_EVENT_SETTINGS", "ALWAYS_FIRST_PERSON", "ALWAYS_THIRD_PERSON"};
		constexpr const char* kPosition[] = {"STANDING", "SEATED"};
		// mod_settings.h's PlayPosition default: Seated on the PS5 (the tracker's origin is not the floor), Standing elsewhere
#ifdef __PROSPERO__
		constexpr int kPositionDefault = 1;
#else
		constexpr int kPositionDefault = 0;
#endif
		constexpr const char* kTurn[] = {"SNAP_30", "SNAP_45", "SNAP_60", "SMOOTH_SLOW", "SMOOTH_NORMAL", "SMOOTH_FAST"};
		constexpr const char* kWalk[] = {"CAMERA", "CONTROLLER"};
		constexpr const char* kSwing[] = {"SWING_NORMAL", "SWING_EASY"};
		constexpr float kHudDistances[] = {0.75f, 1.0f, 1.25f, 1.5f, 1.85f, 2.0f, 2.25f, 2.5f};
		constexpr float kHudSizes[] = {0.5f, 0.6f, 0.7f, 0.85f, 1.0f, 1.25f, 1.5f, 1.75f};

		std::string IniPath() { return ps5paths::Root() + "/bettervr_settings.ini"; }
		std::string JsonPath() { return ps5paths::Root() + "/bettervr.json"; }

		std::string ReadText(const std::string& path)
		{
			std::ifstream file(path, std::ios::binary);
			std::ostringstream text;
			text << file.rdbuf();
			return text.str();
		}

		bool WriteText(const std::string& path, const std::string& text)
		{
			const std::string temporary = path + ".tmp";
			{
				std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
				if (!file)
					return false;
				file << text;
				if (!file)
					return false;
			}
			std::error_code ec;
			std::filesystem::rename(temporary, path, ec);
			return !ec;
		}

		// The ini's BetterVR section, read once
		std::map<std::string, std::string>& Ini()
		{
			static std::map<std::string, std::string> values;
			static bool read = false;
			if (!read)
			{
				read = true;
				std::istringstream lines(ReadText(IniPath()));
				bool inside = false;
				for (std::string line; std::getline(lines, line);)
				{
					if (!line.empty() && line.back() == '\r')
						line.pop_back();
					if (!line.empty() && line[0] == '[')
					{
						inside = line == kSection;
						continue;
					}
					const size_t equals = line.find('=');
					if (inside && equals != std::string::npos)
						values[line.substr(0, equals)] = line.substr(equals + 1);
				}
			}
			return values;
		}

		// The section written back with its keys, in place; the file's other sections as they were
		void SaveIni()
		{
			std::istringstream lines(ReadText(IniPath()));
			std::string out, section;
			bool inside = false, written = false;
			auto writeSection = [&] {
				out += std::string(kSection) + "\n";
				for (const auto& [key, value] : Ini())
					out += key + "=" + value + "\n";
				out += "\n";
				written = true;
			};
			for (std::string line; std::getline(lines, line);)
			{
				if (!line.empty() && line.back() == '\r')
					line.pop_back();
				if (!line.empty() && line[0] == '[')
				{
					inside = line == kSection;
					if (inside)
					{
						writeSection();
						continue;
					}
				}
				if (!inside)
					out += line + "\n";
			}
			if (!written)
				writeSection();
			WriteText(IniPath(), out);
		}

		std::string IniValue(const char* key, const char* fallback)
		{
			const auto it = Ini().find(key);
			return it == Ini().end() ? fallback : it->second;
		}
		float IniFloat(const char* key, float fallback)
		{
			const std::string text = IniValue(key, "");
			char* end = nullptr;
			const float value = std::strtof(text.c_str(), &end);
			return text.empty() || end == text.c_str() ? fallback : value;
		}
		bool IniBool(const char* key, bool fallback)
		{
			const std::string text = IniValue(key, fallback ? "true" : "false");
			return text == "true" || text == "1";
		}
		template <size_t N>
		int IniIndex(const char* key, const char* const (&values)[N], int fallback)
		{
			const std::string text = IniValue(key, values[fallback]);
			for (size_t i = 0; i < N; i++)
				if (text == values[i])
					return (int)i;
			return fallback;
		}
		void SetIni(const char* key, const std::string& value)
		{
			Ini()[key] = value;
			SaveIni();
		}
		std::string FloatText(float value)
		{
			char text[32];
			std::snprintf(text, sizeof(text), "%.2f", value);
			return text;
		}
		template <size_t N>
		int Nearest(const float (&values)[N], float value)
		{
			int best = 0;
			for (int i = 1; i < (int)N; i++)
				if (std::fabs(values[i] - value) < std::fabs(values[best] - value))
					best = i;
			return best;
		}

		// bettervr.json, read once
		rapidjson::Document& Json()
		{
			static rapidjson::Document json;
			static bool read = false;
			if (!read)
			{
				read = true;
				json.Parse(ReadText(JsonPath()).c_str());
				if (json.HasParseError() || !json.IsObject())
					json.SetObject();
			}
			return json;
		}
		// Read again before a write: the switches in this file (graphicsPack, reproject) are meant to be
		// added outside the app, and a write from the home screen or Settings > VR must keep them
		void JsonReload()
		{
			rapidjson::Document& json = Json();
			json.Parse(ReadText(JsonPath()).c_str());
			if (json.HasParseError() || !json.IsObject())
				json.SetObject();
		}
		void SaveJson()
		{
			rapidjson::StringBuffer buffer;
			rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
			Json().Accept(writer);
			WriteText(JsonPath(), std::string(buffer.GetString(), buffer.GetSize()) + "\n");
		}
		bool JsonBool(const char* key, bool fallback)
		{
			auto& json = Json();
			return json.HasMember(key) && json[key].IsBool() ? json[key].GetBool() : fallback;
		}
		float JsonFloat(const char* key, float fallback)
		{
			auto& json = Json();
			return json.HasMember(key) && json[key].IsNumber() ? json[key].GetFloat() : fallback;
		}
		template <typename T>
		void SetJson(const char* key, T value)
		{
			JsonReload();
			auto& json = Json();
			if (json.HasMember(key))
				json.RemoveMember(key);
			json.AddMember(rapidjson::Value(key, json.GetAllocator()), rapidjson::Value(value), json.GetAllocator());
			SaveJson();
		}

		std::vector<std::string> Degrees()
		{
			std::vector<std::string> options;
			for (int d = -45; d <= 45; d += 5)
				options.push_back(fmt::format("{:+}°", d));
			options[9] = "0°";
			return options;
		}
		std::vector<std::string> Centimetres()
		{
			std::vector<std::string> options;
			for (int cm = -50; cm <= 50; cm += 5)
				options.push_back(cm == 0 ? std::string("0 cm") : fmt::format("{:+} cm", cm));
			return options;
		}
	}

	bool VrModeOn()
	{
		return JsonBool("vr", false);
	}

	void SetVrMode(bool vr)
	{
		SetJson("vr", vr);
	}

	std::vector<Row> Shell::VrRows()
	{
		using Kind = Row::Kind;
		std::vector<Row> rows;
		auto add = [&](Kind kind, const char* id, const std::string& label, const std::string& description) -> Row& {
			Row row;
			row.kind = kind;
			row.id = id;
			row.label = label;
			row.description = description;
			rows.push_back(row);
			return rows.back();
		};
		auto options = [&](Row& row, std::vector<std::string> list, int index) {
			row.options = std::move(list);
			row.index = std::clamp(index, 0, (int)row.options.size() - 1);
			row.value = row.options[row.index];
		};
		auto toggle = [&](const char* id, const std::string& label, bool on, const std::string& description) {
			Row& row = add(Kind::Toggle, id, label, description);
			row.on = on;
			row.value = on ? Tr("On") : Tr("Off");
		};

		// tr: Settings > VR: what the headset shows: Breath of the Wild in 3D through BetterVR, or the TV's picture on a screen
		Row& picture = add(Kind::Segmented, "bvr-picture", Tr("In the headset"), Tr("The game in 3D around you, or the TV's picture on a screen in front of you."));
		options(picture, {Tr("The game in 3D"), Tr("A flat screen")}, JsonBool("bettervr", true) ? 0 : 1);

		Row& camera = add(Kind::Segmented, "bvr-camera", Tr("Camera"), Tr("First person is the one made for VR."));
		options(camera, {Tr("First person"), Tr("Third person")}, IniIndex("CameraMode", kCamera, 0));
		Row& cutscenes = add(Kind::Choice, "bvr-cutscenes", Tr("Cutscenes"), Tr("Mixed shows each scene as it works best."));
		options(cutscenes, {Tr("Mixed"), Tr("First person"), Tr("Third person")}, IniIndex("CutsceneCameraMode", kCutscenes, 0));
		Row& position = add(Kind::Segmented, "bvr-position", Tr("Play position"), "");
		options(position, {Tr("Standing"), Tr("Seated")}, IniIndex("PlayPosition", kPosition, kPositionDefault));
		Row& height = add(Kind::Choice, "bvr-height", Tr("Height"), Tr("Raises or lowers your eyes in the game."));
		options(height, Centimetres(), (int)std::lround(IniFloat("PlayerHeightOffset", 0) * 100 / 5) + 10);

		Row& turn = add(Kind::Choice, "bvr-turn", Tr("Turning"), Tr("Snap turns in steps are easier on the stomach."));
		options(turn, {TrF("Snap {0}", "30°"), TrF("Snap {0}", "45°"), TrF("Snap {0}", "60°"), Tr("Smooth, slow"), Tr("Smooth"), Tr("Smooth, fast")}, IniIndex("TurnMode", kTurn, 4));
		Row& walk = add(Kind::Segmented, "bvr-walk", Tr("Walk towards"), "");
		options(walk, {Tr("Where you look"), Tr("Where the controller points")}, IniIndex("WalkingDirection", kWalk, 0));
		Row& swing = add(Kind::Segmented, "bvr-swing", Tr("Swings"), Tr("Relaxed counts smaller swings as hits."));
		options(swing, {Tr("Normal"), Tr("Relaxed")}, IniIndex("SwingSensitivity", kSwing, 0));
		toggle("bvr-onehit", Tr("One hit per swing"), IniBool("OneHitPerSwing", true), "");
		toggle("bvr-lefthanded", Tr("Left-handed"), IniBool("LeftHanded", false), "");

		toggle("bvr-gaze", Tr("Menus follow your view"), IniBool("UiFollowsGaze", true), "");
		Row& distance = add(Kind::Choice, "bvr-hud-distance", Tr("Menu distance"), "");
		std::vector<std::string> distances;
		for (float d : kHudDistances)
			distances.push_back(fmt::format("{:.2f} m", d));
		options(distance, distances, Nearest(kHudDistances, IniFloat("HudDistance", 1.85f)));
		Row& size = add(Kind::Choice, "bvr-hud-size", Tr("Menu size"), "");
		std::vector<std::string> sizes;
		for (float s : kHudSizes)
			sizes.push_back(fmt::format("{}%", (int)std::lround(s * 100)));
		options(size, sizes, Nearest(kHudSizes, IniFloat("HudSize", 0.85f)));
		toggle("bvr-blackbars", Tr("Black bars in cutscenes"), IniBool("UseBlackBarsForCutscenes", false), "");

		Row& grip = add(Kind::Choice, "bvr-grip", Tr("Hand angle"), Tr("Tilts Link's hands against the Sense controllers, if they point wrong."));
		options(grip, Degrees(), (int)std::lround(JsonFloat("gripPitch", 0) / 5) + 9);
		Row& aim = add(Kind::Choice, "bvr-aim", Tr("Aim angle"), Tr("Tilts where the bow and the hands aim."));
		options(aim, Degrees(), (int)std::lround(JsonFloat("aimPitch", 0) / 5) + 9);

		add(Kind::Action, "bvr-reset", Tr("Back to BetterVR's defaults"), Tr("Every setting on this page as BetterVR comes."));
		return rows;
	}

	bool Shell::VrApplyRow(const Row& row, int step, bool cross)
	{
		const std::string& id = row.id;
		if (row.kind == Row::Kind::Toggle)
		{
			if (!cross && step != 0 && ((step > 0) == row.on))
			{
				m_feedback.Play(ui::Cue::Edge);
				return false;
			}
			const std::string value = row.on ? "false" : "true";
			if (id == "bvr-onehit")
				SetIni("OneHitPerSwing", value);
			else if (id == "bvr-lefthanded")
				SetIni("LeftHanded", value);
			else if (id == "bvr-gaze")
				SetIni("UiFollowsGaze", value);
			else if (id == "bvr-blackbars")
				SetIni("UseBlackBarsForCutscenes", value);
			m_feedback.Play(ui::Cue::Focus);
			return true;
		}
		if (row.kind == Row::Kind::Action)
		{
			if (!cross)
				return false;
			for (const char* key : {"CameraMode", "CutsceneCameraMode", "PlayPosition", "PlayerHeightOffset", "TurnMode", "WalkingDirection",
					 "SwingSensitivity", "OneHitPerSwing", "LeftHanded", "UiFollowsGaze", "HudDistance", "HudSize", "UseBlackBarsForCutscenes"})
				Ini().erase(key);
			SaveIni();
			JsonReload();
			auto& json = Json();
			for (const char* key : {"bettervr", "gripPitch", "aimPitch"})
				if (json.HasMember(key))
					json.RemoveMember(key);
			SaveJson();
			m_feedback.Play(ui::Cue::Select);
			Toast(Tr("VR settings as BetterVR comes"));
			return true;
		}
		const int count = (int)row.options.size();
		if (row.kind == Row::Kind::Choice && cross)
		{
			const Row chosen = row;
			OpenPicker(Tr("VR"), row.label, row.options, row.index, [this, chosen](int index) { VrSetIndex(chosen, index); });
			return false;
		}
		int index = row.index + (cross ? 1 : step);
		if (row.kind == Row::Kind::Choice || cross)
			index = (index + count) % count;
		else if (index < 0 || index >= count)
		{
			m_feedback.Play(ui::Cue::Edge);
			return false;
		}
		VrSetIndex(row, index);
		m_feedback.Play(ui::Cue::Focus);
		return true;
	}

	void Shell::VrSetIndex(const Row& row, int index)
	{
		const std::string& id = row.id;
		if (id == "bvr-picture")
			SetJson("bettervr", index == 0);
		else if (id == "bvr-camera")
			SetIni("CameraMode", kCamera[index]);
		else if (id == "bvr-cutscenes")
			SetIni("CutsceneCameraMode", kCutscenes[index]);
		else if (id == "bvr-position")
			SetIni("PlayPosition", kPosition[index]);
		else if (id == "bvr-height")
			SetIni("PlayerHeightOffset", FloatText((index - 10) * 0.05f));
		else if (id == "bvr-turn")
			SetIni("TurnMode", kTurn[index]);
		else if (id == "bvr-walk")
			SetIni("WalkingDirection", kWalk[index]);
		else if (id == "bvr-swing")
			SetIni("SwingSensitivity", kSwing[index]);
		else if (id == "bvr-hud-distance")
			SetIni("HudDistance", FloatText(kHudDistances[index]));
		else if (id == "bvr-hud-size")
			SetIni("HudSize", FloatText(kHudSizes[index]));
		else if (id == "bvr-grip")
			SetJson("gripPitch", (index - 9) * 5);
		else if (id == "bvr-aim")
			SetJson("aimPitch", (index - 9) * 5);
	}
}
