// SPDX-License-Identifier: GPL-3.0-or-later
// BotW VR (docs/bettervr-ps5): the app's home, in place of the Library. One game: its own boot
// screen (meta/bootTvTex, from the player's dump) across the screen, its name, and a short menu:
// Play in VR, Play on TV, VR settings (Settings > VR, vr_settings.cpp), Graphic packs (the game's) and
// Settings. Without the game, where to put it. The Library's ways back (Settings, Graphic packs,
// a game's end) all come back here.

#include "shell_internal.h"
#include "../../app/emulator.h"
#include "../../app/paths.h"

#include <algorithm>

namespace ps5shell
{
	using namespace ui::tokens;

	namespace
	{
		constexpr uint32_t kWhite = 0xffffffff, kWhite70 = 0xb3ffffff, kWhite50 = 0x80ffffff, kInkDark = 0xff0b141b;
		constexpr uint32_t kTeal = 0xff78d2be; // the app's own art's (tools/render-botwvr-art.py)
		constexpr float kLeft = 128, kMenuTop = 452, kItemW = 620, kItemH = 92, kItemGap = 14;

		enum class Item
		{
			PlayVr,
			PlayTv,
			VrSettings,
			Packs,
			Settings,
			LookAgain,
		};

		std::vector<Item> Items(bool game)
		{
			if (game)
				return {Item::PlayVr, Item::PlayTv, Item::VrSettings, Item::Packs, Item::Settings};
			return {Item::LookAgain, Item::Settings};
		}
	}

	int Shell::BotwGame() const
	{
		for (const uint64_t titleId : {0x00050000101C9500ull, 0x00050000101C9400ull, 0x00050000101C9300ull})
			if (const int index = FindGame(titleId); index >= 0)
				return index;
		return -1;
	}

	void Shell::OpenSettingsPage(const std::string& page)
	{
		m_feedback.Play(ui::Cue::Select);
		ShowTab(ScreenId::Settings);
		m_page = page;
		m_onRows = true;
		m_settingRow = 0;
	}

	void Shell::HomeUpdate(const ui::Press& press)
	{
		const int game = BotwGame();
		const std::vector<Item> items = Items(game >= 0);
		m_homeItem = std::clamp(m_homeItem, 0, (int)items.size() - 1);
		switch (press.button)
		{
		case Button::Up:
		case Button::Down:
		{
			const int next = m_homeItem + (press.button == Button::Down ? 1 : -1);
			if (next < 0 || next >= (int)items.size())
				m_feedback.Play(ui::Cue::Edge, press.repeat);
			else
			{
				m_homeItem = next;
				m_feedback.Play(ui::Cue::Focus);
			}
			break;
		}
		case Button::Cross:
			switch (items[m_homeItem])
			{
			case Item::PlayVr:
			case Item::PlayTv:
				SetVrMode(items[m_homeItem] == Item::PlayVr);
				Launch(game);
				break;
			case Item::VrSettings: OpenSettingsPage("vr"); break;
			case Item::Packs:
				if (!m_status.coreReady)
				{
					m_feedback.Play(ui::Cue::Denied);
					Toast(Tr("Cemu is still starting"));
					break;
				}
				m_feedback.Play(ui::Cue::Select);
				PacksOpen(game, ScreenId::Library);
				break;
			case Item::Settings: OpenSettingsPage(game >= 0 ? "graphics" : "files"); break;
			case Item::LookAgain:
				m_feedback.Play(ui::Cue::Select);
				LookForGames();
				break;
			}
			break;
		default: break;
		}
	}

	std::vector<Hint> Shell::HomeHints() const
	{
		return {{Icon::UpDown, Tr("Move")}, {Icon::Cross, Tr("Choose")}};
	}

	void Shell::HomeDraw(Canvas& canvas)
	{
		const Box screen{0, 0, 1920, 1080};
		const int index = BotwGame();
		Game* game = index >= 0 ? &m_games[index] : nullptr;

		// the game's boot screen, across the screen; until it is read, the app's own colours
		canvas.Rect(screen, 0, 0xff081016);
		const std::string picture = game ? BackdropOf(*game) : std::string();
		const ui::Picture* image = picture.empty() ? nullptr : &m_images->Get(picture, false);
		if (image && image->texture)
			canvas.ImageCover(image->texture, image->width, image->height, screen, 0, kWhite);
		else
		{
			canvas.LinearGradient(screen, 0, 0xff123446, 0xff080e16, 0, 0, 0, 1080);
			canvas.RadialGradient(screen, 0, 0x5578d2be, 0x0078d2be, 1300, 380, 900, 620);
		}
		// shade on the left for the menu, and at the foot
		canvas.LinearGradient(screen, 0, 0xf2060c12, 0x00060c12, 0, 0, 1280, 0);
		canvas.LinearGradient(screen, 0, 0x00060c12, 0xd9060c12, 0, 620, 0, 1080);
		canvas.LinearGradient(screen, 0, 0x99060c12, 0x00060c12, 0, 0, 0, 220);

		// its name
		canvas.PushAlpha(Enter(0));
		canvas.Text(Style(kOverlineStyle), kLeft, 150, "The Legend of Zelda", kTeal, 1200, 1);
		ui::TextStyle title = Style(kDisplayStyle);
		title.size = 104;
		const ui::TextBlock name = canvas.Text(title, kLeft, 182, "Breath of the Wild", kWhite, 1300, 1);
		// a VR mark beside it
		const Box mark{kLeft + name.width + 28, 210, 108, 64};
		canvas.Rect(mark, 32, kTeal);
		ui::TextStyle markStyle = Style({40, ui::Weight::Bold, 1.0f, 2.0f});
		const ui::TextBlock vr = m_fonts.Layout(markStyle, "VR", mark.w, 1);
		canvas.Text(vr, mark.CentreX() - vr.width * 0.5f, mark.CentreY() - vr.height * 0.5f, kInkDark);

		// what the player has: the update and DLC, or what is missing
		const ui::TextStyle facts = Style({28, ui::Weight::Medium, 1.3f});
		std::string line;
		if (game)
		{
			const auto& g = game->entry.game;
			line = g.hasUpdate ? TrF("Update v{0}", g.version) : fmt::format("v{}", g.version);
			if (g.dlcCount)
				line += std::string(" · ") + Tr("DLC");
			if (!g.hasUpdate || g.version < 208)
				// tr: BotW VR's home, when Breath of the Wild has no update v208: BetterVR's patches need it
				line += std::string(" · ") + Tr("BetterVR needs update v208");
		}
		else
			line = m_status.coreReady ? Tr("Breath of the Wild is not here yet") : Tr("Cemu is still starting");
		canvas.Text(facts, kLeft, 330, line, kWhite70, 1300, 1);
		canvas.PopAlpha();

		// the menu
		canvas.PushAlpha(Enter(1));
		canvas.PushOffset(0, (1 - Enter(1)) * 20);
		const std::vector<Item> items = Items(game != nullptr);
		m_homeItem = std::clamp(m_homeItem, 0, (int)items.size() - 1);
		float y = kMenuTop;
		if (!game)
		{
			// where the game goes
			const std::string where = TrF("Put the Wii U game, with its update v208, in {0}. One WUA file can hold both.", ps5paths::Games());
			const ui::TextBlock text = canvas.Text(Style({30, ui::Weight::Regular, 1.4f}), kLeft, y, where, kWhite, 900, 4);
			y += text.height + 48;
		}
		for (int i = 0; i < (int)items.size(); i++)
		{
			const bool focused = i == m_homeItem && !m_behind;
			const Box box{kLeft, y, kItemW, kItemH};
			Icon icon = Icon::Play;
			std::string label, detail;
			switch (items[i])
			{
			case Item::PlayVr:
				label = Tr("Play in VR");
				// tr: BotW VR's home, under Play in VR
				detail = Tr("In the PS VR2 headset, with the Sense controllers");
				break;
			case Item::PlayTv:
				label = Tr("Play on TV");
				// tr: BotW VR's home, under Play on TV
				detail = Tr("Flat, with the DualSense");
				break;
			case Item::VrSettings:
				icon = Icon::Sparkle;
				// tr: BotW VR's home: Settings > VR
				label = Tr("VR settings");
				detail = Tr("Camera, turning, menus and hands");
				break;
			case Item::Packs:
				icon = Icon::Grid;
				label = Tr("Graphic packs");
				// tr: BotW VR's home, under Graphic packs
				detail = Tr("The community's, for this game");
				break;
			case Item::Settings:
				icon = Icon::Gear;
				label = Tr("Settings");
				// tr: BotW VR's home, under Settings
				detail = Tr("Picture, sound, controls, saves and files");
				break;
			case Item::LookAgain:
				icon = Icon::Search;
				// tr: BotW VR's home without the game: look in the games folder again
				label = Tr("Look for it again");
				detail = ps5paths::Games();
				break;
			}
			const bool primary = items[i] == Item::PlayVr;
			canvas.Rect(box, kItemH * 0.5f, focused ? kWhite : primary ? 0x4078d2be : 0x1fffffff);
			if (!focused)
				canvas.Ring(box, kItemH * 0.5f, 1.5f, primary ? 0x9978d2be : 0x33ffffff);
			const uint32_t ink = focused ? kInkDark : kWhite;
			canvas.Draw(icon, {box.x + 36, box.CentreY() - 17, 34, 34}, focused ? kInkDark : primary ? kTeal : kWhite70);
			canvas.Text(Style({32, ui::Weight::SemiBold, 1.0f}), box.x + 96, box.y + 16, label, ink, kItemW - 130, 1);
			canvas.Text(Style({21, ui::Weight::Regular, 1.0f}), box.x + 96, box.y + 56, detail, focused ? 0xa60b141b : kWhite50, kItemW - 130, 1);
			y += kItemH + kItemGap;
		}
		canvas.PopOffset();
		canvas.PopAlpha();

		// what this is, small, at the foot
		canvas.Text(Style({19, ui::Weight::Medium, 1.0f}), kLeft, 1014,
			std::string("BotW VR · BetterVR 0.9.23 · ") + Tr("Version") + " " + PS5CEMU_VERSION, kWhite50, 900, 1);
	}
}
