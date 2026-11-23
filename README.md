# BotW VR on PS5

An experimental port of [BotW-BetterVR](https://github.com/Crementif/BotW-BetterVR) (the Breath of
the Wild VR mod for Cemu) to a jailbroken PS5, shown on PlayStation VR2 with the Sense controllers.
It is a fork of [PlayTendoh](https://github.com/premohq/PlayTendoh) (formerly PS5-ECAM), whose native Cemu it runs in.
For what PlayTendoh itself does (the launcher, other systems), see its repository.

**Status: experimental.** Version 0.7.0 runs BetterVR 1.0.0 on Crementif's Cemu fork. Tested on
firmware 13.60: the game runs in VR on PS VR2 with head tracking and the Sense controllers (vibration,
set per category and device on the app's Vibration page, headset rumble on damage, the bow's adaptive
trigger), Options short press for the pause and a one-second hold to recenter, and about 60 fps at 1x;
this also holds with kstuff and no etaHEN, where VR starts through the sandbox path. VR Resolution
goes from 0.5x to 1.5x. v0.7.0 also tested on 13.60: the five Eye Aim Assist steps, Eye Magnesis, Eye
Rune Pick, Magnesis push and pull by hand, and the respawn camera after a fall in a shrine. Everything
else, Play on TV included, is untested on console. The newest builds may be untested on a console;
[docs/bettervr-ps5/STATUS.md](docs/bettervr-ps5/STATUS.md) says which.

## What you need

- A PS5 that runs homebrew (a HEN, elfldr and ShadowMount Plus). With etaHEN, add the app's title ID
  `PPSA98720` to the HEN's app jailbreak list. Setups with kstuff and no app jailbreak list also work
  on firmware 13.60; see [docs/HEN-SETUP.md](docs/HEN-SETUP.md).
- A PS VR2 headset set up in the PS5's settings, and both Sense controllers.
- Your own copy of **The Legend of Zelda: Breath of the Wild, Wii U version**, with update v208,
  dumped from your own disc or console.

No game files, keys, firmware or Sony SDK material are provided here, and none will be.

## Install

1. Download the zip from the [releases](https://github.com/Papyszoo/botw-bettervr-ps5/releases).
   Full releases (`v0.7.0` is the latest) are the stable choice. The rolling `testing` pre-release is
   replaced for each new build and carries the newest changes; its notes start with "Untested on
   console" until a console result is recorded in STATUS.md.
2. Follow [docs/bettervr-ps5/INSTALL.md](docs/bettervr-ps5/INSTALL.md): the game's formats, where the
   files go, the HEN list entry and the VR settings. Unzipping a release over the app folder keeps
   settings and saves.

To build it yourself, see [docs/BUILDING.md](docs/BUILDING.md).

## Known issues

- A one-frame body flash can show while strafing with the stick (a known BetterVR 1.0 issue).
- NPC dialogue panels can be darkened by world shadows (a known BetterVR 1.0 issue).
- The eye rows need eye tracking on and the headset calibrated in the PS5's Settings > Accessories >
  PS VR2; without a gaze sample they are hidden, and Magnesis and Stasis pick from the middle of the view.
- On firmware other than 13.60 the Sense controllers' vibration and triggers stay off and VR runs without
  them. The headset motor covers 13.60, 12.40, 11.60, 10.00, 9.00, 8.40, 7.61 and 5.50, and the 9.00 and
  12.40 rows, with the Sense import fallback, are untested on a console.
- The game's name keyboard (for example when naming a horse) does not show in VR. Workaround: on Breath
  of the Wild's page, switch to **Play on TV**, enter the name with the DualSense, then switch back to
  **Play in VR**.
- The launcher's Graphic packs page changes the TV picture only; VR uses its own graphics settings, and
  BetterVR's Graphics pack is added only at Play in VR.
- The Extended Memory graphic pack is off by default.
- Open reports: [#2](https://github.com/Papyszoo/botw-bettervr-ps5/issues/2) (feedback and
  troubleshooting) and [#3](https://github.com/Papyszoo/botw-bettervr-ps5/issues/3) (a crash at launch
  in VR on firmware 13.20, untested since the Sense vibration was turned off there in v0.5.1).

## Credits

- [premohq](https://github.com/premohq/PlayTendoh), [JMUtechnologies](https://github.com/JMUtechnologies)
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
