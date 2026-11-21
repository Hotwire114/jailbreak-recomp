# Jailbreak Recomp on Linux and Steam Deck (preview)

This is the first native Linux version. It does the same thing as the Windows one: you bring
your own Road Rash: Jailbreak (USA) disc image, and the game is compiled on your own PC. Nothing
from the game is in this download.

It's a **preview**: it has been built and checked, but not yet played on a real Linux PC or
Steam Deck. If something goes wrong, please open an issue on GitHub and attach `setup.log`,
`build.log` and `runtime.log` from the game folder (see below).

## What you need

- 64-bit Linux with glibc 2.35 or newer: Ubuntu 22.04 or newer, Fedora 36+, Arch, Linux Mint 21+,
  SteamOS 3 (Steam Deck), and similar.
- Python 3 (most systems already have it; SteamOS does). Only the first setup uses it.
- Working OpenGL graphics drivers (any recent Mesa, NVIDIA or AMD driver).
- PulseAudio's client library (`libpulse0`) for sound. Desktop systems and SteamOS have it, and it
  works with PipeWire too. On a minimal install: `sudo apt install libpulse0` (or your distro's
  `libpulse`).
- About 12 GB of free space for the first setup, and an internet connection for it.
- Your own Road Rash: Jailbreak (USA, SLUS-01053) disc image: `.bin`/`.cue` or `.iso`.

## Install

1. Extract `JailbreakRecomp_<version>_Linux.tar.gz` anywhere, for example `~/Games/`.
2. Open the `JailbreakRecomp` folder and run `RoadRashJailbreak`
   (or in a terminal: `~/Games/JailbreakRecomp/RoadRashJailbreak`).
3. Choose **Find your game (.bin)** and pick your `.bin`, `.cue` or `.iso`. You can also drag the
   file onto the launcher window. It's checked against the original disc and copied once.
4. Setup starts on its own. The first time it downloads the build tools (about 800 MB, once),
   then compiles the game on your PC. That takes roughly 15 to 40 minutes depending on your
   CPU. Keep the window open.
5. Choose **Play**.

## Steam Deck

Do the first setup in **Desktop Mode** (the file picker needs it):

1. Extract the download into e.g. `~/Games/`, run `RoadRashJailbreak` and follow the steps
   above. Keep the Deck plugged in during the compile.
2. To play from Game Mode: in Steam, *Games > Add a Non-Steam Game to My Library > Browse*, pick
   `RoadRashJailbreak` in the `JailbreakRecomp` folder and add it. **Don't** turn on a Proton
   compatibility tool for it: it's a native Linux program.
3. In Game Mode the launcher works with the Deck's buttons (D-pad and A), and the game uses the
   Deck's controls as a controller.

## Where things are

Your game folder (disc copy, build, saves and settings) is
`~/.local/share/RoadRashJailbreak` (or `$XDG_DATA_HOME/RoadRashJailbreak`). Logs: `setup.log`,
`build.log` and `runtime.log` there.

To remove everything, delete that folder and the `JailbreakRecomp` folder.

## What's different from Windows (for now)

- It's the same launcher as on Windows (Saves, Cheats, Options, Original mode and the rest), but
  online play, mods and checking for updates aren't in the Linux version yet. Grab new versions
  from the GitHub releases page.
- Text boxes (rider names, custom cheats) are typed straight into the launcher: Enter for OK,
  Esc to cancel, Ctrl+V to paste.
- No Discord status unless Discord is running natively (not as a Flatpak without its socket).

## If the game is slow

If the game crawls while your CPU is maxed out, it's being drawn by Mesa's software renderer
(llvmpipe) instead of your graphics card. Install your GPU's driver (on NVIDIA, the proprietary
driver). Under WSL on Windows, start it with `GALLIUM_DRIVER=d3d12` set.

## Options

```
RoadRashJailbreak --import file.bin|file.cue|file.iso   import a disc image
RoadRashJailbreak --play-on-start                      start the game straight away when it's ready
RoadRashJailbreak --data-root /some/folder             keep the game folder somewhere else
RoadRashJailbreak --version | --help
```
