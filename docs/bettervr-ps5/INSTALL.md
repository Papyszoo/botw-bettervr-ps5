# Breath of the Wild in VR on PS5: what you need and where it goes

Everything needed to play BotW-BetterVR on a jailbroken PS5 with PlayStation VR2, from this fork's
pre-releases. **Nobody has run these builds on a console yet** (see `STATUS.md`): this page says how
they are meant to be set up, not what has been seen to work.

Nothing from Nintendo or Sony comes with the app. You bring the game, dumped from your own Wii U
disc or console.

## 1. What you need

| | What | Notes |
| --- | --- | --- |
| Console | A PS5 that runs homebrew, with a HEN and elfldr | The same setup PS5-ECAM needs ([HEN setup](../HEN-SETUP.md)). |
| HEN list | `PPSA99360` in your HEN's app jailbreak list | Already there if PS5-ECAM runs on your console. Without it Cemu gets no JIT and runs far too slowly for VR. |
| Headset | PS VR2, plugged in and set up in the PS5's settings | Its first-time setup (play area, eye distance) done once from the PS5's own menus. |
| Controllers | Both PS VR2 Sense controllers, paired and charged | From `m4-sense` on. A DualSense works too, as the Wii U GamePad. |
| Game | **The Legend of Zelda: Breath of the Wild, Wii U version** | Any region: USA, Europe or Japan. Not the Switch version. |
| Update | **Update v208** (game version 1.5.0), the last one | BetterVR's code patches are written for v208 only. With an older update, or none, it won't work. |
| DLC | Optional | The Expansion Pass works if you have it, but isn't needed. |
| Keys | **Only** for encrypted WUD/WUX disc images | WUA files and unpacked games need no keys. See section 3. |

No Wii U system files are needed (`otp.bin`, `seeprom.bin` and the like are only for Nintendo
Network, which BotW doesn't use). No PS5 keys, firmware files or PS5 SDK either.

## 2. The game's format

Use one of these. They are listed from simplest to most work.

| Format | What it looks like | Keys? | Update and DLC |
| --- | --- | --- | --- |
| **WUA** (recommended) | One `.wua` file | No | Can be inside the same WUA: one file with game, v208 and DLC. |
| **Unpacked game** | A folder with `code/`, `content/`, `meta/` (decrypted) | No | Installed separately (section 4). |
| **Unpacked game in an archive** | That folder in a `.zip`, `.7z`, `.rar` or `.tar.*`, one game per archive | No | Installed separately; not read from archives. |
| **WUD / WUX** | A disc image, `.wud` or `.wux` | **Yes:** the disc key | Installed separately. |
| **Packed title** | A folder with `title.tmd`, `title.tik` and `.app` files | No, but `title.tik` must be there | Cemu reads it as it is. |

What a dump from your own console gives you depends on the tool. **dumpling** (on a Wii U with
Aroma/Tiramisu) gives unpacked folders for the game, its update and its DLC. Cemu on a PC (version
2.0 or newer) can then turn the three into one WUA: **Tools > Title Manager**, right-click the game,
**Convert to compressed Wii U archive (.wua)**. That WUA is the easiest thing to bring over.

BotW's title IDs, if a tool asks or to check that a dump is complete:

| | USA | Europe | Japan |
| --- | --- | --- | --- |
| Game | `00050000101C9400` | `00050000101C9500` | `00050000101C9300` |
| Update (v208) | `0005000E101C9400` | `0005000E101C9500` | `0005000E101C9300` |
| DLC | `0005000C101C9400` | `0005000C101C9500` | `0005000C101C9300` |

## 3. Where the files go

The Wii U side's folder is **`/data/cemu`** on an install made with 5.1 or newer. An older install
keeps using **`/data/ps5cemu`** until you choose Reorganize. **Settings > Game files** on the Wii U
side shows which folder yours uses. Below, `<cemu>` is that folder.

```text
<cemu>/
├── games/                          the game: a .wua, a .wud/.wux, or an unpacked folder
│   └── The Legend of Zelda Breath of the Wild.wua
├── updates/                        an unpacked update v208 (if it isn't inside your WUA)
│   └── BotW Update v208/
│       ├── code/  content/  meta/
├── dlc/                            an unpacked DLC (optional)
│   └── BotW DLC/
│       ├── code/  content/  meta/
└── keys.txt                        only for an encrypted WUD/WUX
```

Folder and file names don't matter: the app reads the title ID inside each one. `games/` can be
another folder, or a USB drive: pick it in **Settings > Game files**.

**keys.txt** (only for WUD/WUX). One line per disc. The line is the disc key as 32 hexadecimal
characters, optionally followed by a comment:

```text
00112233445566778899aabbccddeeff # Breath of the Wild (EUR)
```

The disc key is the one your disc-dumping tool saved next to the image, often as `game.key` (16
bytes). Turn it into hex with `xxd -p game.key` on a PC.

**The VR switch** is in the launcher's folder, **`/data/ps5cemu`**, even on an install whose Wii U
folder is `/data/cemu`. (With the app installed on a USB drive, it is the drive's
`PS5CEMU-HAR-data` folder instead.)

```text
/data/ps5cemu/bettervr.json
```

```json
{"vr": true}
```

### Getting the files onto the PS5

Any of these:

- **FTP**, from a PC, with your HEN's FTP server.
- **A USB drive** (exFAT): put the game in a folder on it and choose that folder in **Settings > Game
  files**, or copy it across with a file manager on the PS5.
- **The app's upload page**, from a phone or PC browser: on the Wii U side turn on **Settings > System
  > Remote touch screen**, then open `http://<the PS5's address>:8080/files`, choose the Wii U games
  folder and send the file. It takes WUA, WUD/WUX and archives, not loose folders. `bettervr.json` and `keys.txt` go over
  FTP.

## 4. Install the update (and DLC)

Skip this if your WUA already has update v208 in it.

1. Start the app and go to the **Wii U** side (touchpad click switches sides).
2. **Settings > Game files > Updates, DLC and mods.** It lists what it found in `<cemu>/updates`,
   `<cemu>/dlc`, your games folder, USB drives and `/data`.
3. Choose the update with Cross (or Square for everything new). It is copied into Cemu's storage
   (`<cemu>/mlc01`). This takes a few minutes for BotW's 3 GB update.
4. In the Library, Breath of the Wild's page should now show update **v208**.

## 5. Install this build

1. From the pre-release (the newest is `m4-sense`), download `PS5-ECAM-v5.1.1.zip` and `SHA256SUMS`,
   and check them on a PC:

   ```bash
   sha256sum -c SHA256SUMS --ignore-missing
   ```

2. Copy the zip's `PPSA99360` folder over `/data/homebrew/PPSA99360`. It replaces PS5-ECAM (same
   title ID); your games, saves and settings stay.
3. **If the app offers an update, decline it**, with **Skip this version** so it stops asking.
   Accepting would put premohq's PS5-ECAM back over this build.

## 6. First start, flat

Do this once without VR, so a problem with the game itself doesn't look like a VR problem.

1. With no `bettervr.json` (or `{"vr": false}`), start the app, open the Wii U side and start BotW.
   Check that it gets in-game. The first start builds shaders and stutters; that's normal.
2. Back in the Library: **Options** on the game > **Graphic packs** >
   **The Legend of Zelda: Breath of the Wild**. Turn **Mods > FPS++** on, with its FPS limit at 120
   or higher. **BetterVR crashes without FPS++.**
3. Quit to the Library.

## 7. VR

1. Put `{"vr": true}` in `/data/ps5cemu/bettervr.json`.
2. **Close the app and start it again.** BetterVR's graphic pack is put in place when the app starts.
3. In BotW's graphic packs, **Better VR** should now be listed and on, next to FPS++.
4. Turn on both Sense controllers, put on the headset, and start the game.

Optional settings in the same file:

| Setting | What it does |
| --- | --- |
| `{"vr": true, "bettervr": false}` | No BetterVR: the game as a flat screen in the headset (M2), to compare. |
| `"gripPitch": 30`, `"aimPitch": 30` | Tilt the controllers' poses, in degrees, if Link's hands point the wrong way. |

BetterVR's own menu, in the game: long-press the right stick (R3).

To play flat again, set `{"vr": false}` and restart the app.

## 8. If something goes wrong

- **The game isn't in the Library:** **Settings > Game files > Look for games now.** An encrypted
  WUD/WUX without its key says *"Its disc key is missing from …/keys.txt"* when started.
- **Better VR isn't in the graphic packs:** the app wasn't restarted after `bettervr.json` changed,
  or the file isn't valid JSON (check the quotes).
- **It crashes at the title screen with BetterVR:** FPS++ off, or an update other than v208.
- **Logs:** Settings > Diagnostics > **Copy logs to USB**, or `/data/ps5cemu/logs/boot.log` over FTP.
  VR's lines start with `[vr]`, `[vr-xr]` and `[bettervr]`. `STATUS.md`'s Test requests say which
  ones to send.
