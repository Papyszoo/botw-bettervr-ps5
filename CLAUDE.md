# BotW-BetterVR on PS5 (PSVR2) — notes for Claude

This repository is Patryk's **fork of PS5-ECAM** (premohq/PS5-ECAM). Its goal is to run
Crementif's BotW-BetterVR (the Breath of the Wild VR mod for Cemu) inside PS5-ECAM's native Cemu
on a jailbroken PS5, displayed on PlayStation VR2 through PS5XR, the OpenXR runtime from
theghostonline's HLA-PSVR2/RE7-PSVR2 ports.

Read these before you plan anything:

1. `docs/bettervr-ps5/PLAN.md`: milestones, what "done" means for each, when to stop.
2. `docs/bettervr-ps5/FINDINGS.md`: what has already been checked in the code, with sources.
3. `docs/bettervr-ps5/STATUS.md`: where the work stands. Update it as you go.
4. `AGENTS.md` and `CONTRIBUTING.md`: upstream PS5-ECAM's conventions.

## How upstream's AGENTS.md applies here

- **Follow its code rules:** match the surrounding code (tabs, naming, comment voice); files through
  `ps5files::OwnStatus`; user-facing strings through `Tr`/`TrF`/`TrP` with every `port/lang/*.po`
  updated; settings stay few; no hard-coded side counts; run the host tests for what you touch.
- **Its scope rules do not apply:** this is a personal fork, not a pull request to premohq. "Do not
  add personal ports" and "open an issue first" are about upstream contributions. If Patryk later
  wants to upstream any of this, that is a separate conversation with the owner.
- **Keep upstream rebasable:** keep the `upstream` branch equal to premohq's `PS5-ECAM` branch.
  Keep our changes in as few, well-named places as possible: `port/bettervr/`, `patches/bettervr/`,
  `patches/cemu/` (numbered after upstream's), and `docs/bettervr-ps5/`.

## Hard rules

- **Never claim a console or headset result Patryk did not report.** Nobody in a remote session can
  run anything on the PS5. Every build is "built, untested on console" until he says otherwise.
  When you need a result from the console, write a numbered **Test request** in `STATUS.md`. Say
  exactly what to install, what to do, and which log lines or numbers to send back.
- **No game files, keys, firmware, or Sony SDK material** in the repo, in releases, or downloaded
  in a session. Patryk supplies his own dumped game on the console.
- **The PSVR2 path is `third_party/hmd2/`,** from Patryk's photo-splat-vr, which he has tested on
  his console. PS5XR (`third_party/ps5xr/`) is optional; remote sessions can't download it, because
  theghostonline.dev is blocked.
- **Commit and push often** to `main` (or a topic branch merged into `main`). Remote sessions are
  reclaimed when idle, and anything unpushed is lost.

## Building and long sessions

- **Building:** follow `docs/BUILDING.md` (Ubuntu 24.04, clang-18/lld-18, `make deps`, `make build`,
  `make release`). Install the apt packages from that doc.
- **Network:** git clones from GitHub and GitHub *release* downloads work.
  `github.com/<owner>/<repo>/archive/*.tar.gz` downloads are refused (HTTP 403), so clone instead.
  Other hosts are usually blocked. Don't retry a 403; report it.
- **Long builds:** the machine is small (2 cores at last check). A shell command can run for at
  most 10 minutes, and background processes are stopped soon after a turn ends. Run long builds as
  repeated foreground `ninja`/`make` calls of 9 minutes or less. They're incremental, so each call
  continues the last. Record build times in `STATUS.md`.
- **Disk:** writable space is a fixed allowance. Delete build trees you no longer need, and keep
  `.deps/` once fetched.
- **Test builds:** publish console test builds as GitHub **pre-releases** on this repo. Name them
  after the milestone (`m1-baseline`, `m2-xr-quad`, …), attach the app zip, and write release
  notes that start with "Untested on console".
