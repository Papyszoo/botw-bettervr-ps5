# Breath of the Wild VR on PS5

An experimental port of [BotW-BetterVR](https://github.com/Crementif/BotW-BetterVR) (the Breath of
the Wild VR mod for Cemu) to a jailbroken PS5, shown on PlayStation VR2 with the Sense controllers.
It is a fork of [PS5-ECAM](https://github.com/premohq/PS5-ECAM), whose native Cemu it runs in.
For what PS5-ECAM itself does (the launcher, other systems, settings), see its repository.

**Status: experimental.** The author tests on his own PS5 (firmware 13.59 and 13.60). What he has
reported working on the console: the app starts and the game runs flat at a steady 60 fps; in VR the
he reports smooth head tracking, and the Sense controllers controlling the game.
Known problems are below. Everything newer than that is built but not yet tested on a console;
[docs/bettervr-ps5/STATUS.md](docs/bettervr-ps5/STATUS.md) says which.

## What you need

- A PS5 that runs homebrew (a HEN, elfldr and ShadowMount Plus), with the app's title ID `PPSA98720`
  added to your HEN's app jailbreak list.
- A PS VR2 headset set up in the PS5's settings, and both Sense controllers.
- Your own copy of **The Legend of Zelda: Breath of the Wild, Wii U version**, with update v208,
  dumped from your own disc or console.

No game files, keys, firmware or Sony SDK material are provided here, and none will be.

## Install

1. Download the zip of the newest [release](https://github.com/Papyszoo/botw-bettervr-ps5/releases).
   Releases are marked as pre-releases and say whether they have been tested on a console.
2. Follow [docs/bettervr-ps5/INSTALL.md](docs/bettervr-ps5/INSTALL.md): the game's formats, where the
   files go, the HEN list entry and the VR settings.

To build it yourself, see [docs/BUILDING.md](docs/BUILDING.md).

## Known issues

- As of the last console report, Link's physics is still off in VR (movement is delayed or gets launched); flat play is normal.
- Some visual glitches in VR, such as the two eyes disagreeing in menus, scopes and some lighting.
  Fixes for several of these are built but not yet reported back from a console.
- The app's launcher is the emulator's, not made for one game.

## Credits

- [premohq](https://github.com/premohq/PS5-ECAM), [JMUtechnologies](https://github.com/JMUtechnologies)
  and the PS5-ECAM contributors: this fork is built on their work. See PS5-ECAM's own credits for the
  emulators, libraries and PS5 homebrew work it relies on.
- [Cemu](https://github.com/cemu-project/Cemu) by the Cemu team (MPL-2.0).
- [BotW-BetterVR](https://github.com/Crementif/BotW-BetterVR) by Crementif (MIT), which the patches in
  `patches/bettervr` change; its licence is in
  [patches/bettervr/LICENSE-BetterVR](patches/bettervr/LICENSE-BetterVR). The Linux ports by
  ctrlaltdle, kebabskal and mitch030504 helped too.
- The PS VR2 code in `third_party/hmd2` comes from the author's photo-splat-vr, with code from
  theghostonline's PS5VR and HLA-PSVR2 (GPL-3.0-or-later); the notices are in
  [third_party/hmd2/THIRD_PARTY_NOTICES.md](third_party/hmd2/THIRD_PARTY_NOTICES.md).
- [GameTDB](https://www.gametdb.com/) for game information (see [port/app/gametdb/NOTICE](port/app/gametdb/NOTICE)),
  and the [Lexend](https://github.com/googlefonts/lexend) authors for the font (SIL OFL 1.1).

## License

GPL-3.0-or-later, see [LICENSE](LICENSE). Files derived from Cemu keep Cemu's MPL-2.0, and the
BetterVR patches keep BetterVR's MIT notice. Other third-party components keep their own licenses.

The app's content ID and title text use the names they had from the start, for compatibility with
existing installs.

This is an independent homebrew project, not affiliated with or endorsed by Sony Interactive
Entertainment, Nintendo, or the Cemu, BetterVR or PS5-ECAM projects. "PlayStation", "PS5" and "PS VR2"
are trademarks of Sony Interactive Entertainment Inc.; "Wii U", "Nintendo Switch" and "The Legend of
Zelda" are trademarks of Nintendo. No firmware, keys, games or SDK material is included. It is
provided as is, without warranty, for use with hardware and games you own.
