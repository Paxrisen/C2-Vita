<p align="center"><img src="pkg/sce_sys/icon0.png" width="96" alt="C2-Vita icon"></p>

# C2-Vita

**An unofficial PS Vita runtime for Civilization II: Multiplayer Gold Edition (PC).**

C2-Vita runs the original Windows version of the game on a PS Vita. It ships
**no game files and no game code**. You need your own legally obtained PC copy
of the game.

> Not affiliated with or endorsed by the rights holders. "Civilization" is a
> trademark of its respective owners.

---

## Features

- Runs the original game data, including the German and English editions (tested: MGE 5.4.0f German)
- Two ways to run the game code:
  - **Public VPK**: the game code runs in a built-in x86 emulator (winx86 / Box86 dynarec)
  - **Native VPK (via patcher)**: the patcher translates *your own* `civ2.exe` into native ARM code on your PC, so the game code runs natively on the Vita
- Full controller support: both analog sticks as mouse pointer, configurable buttons, quick-command menu
- Touchscreen support (tap = click, or pointer-only mode)
- On-screen keyboard for city names, player names and file names
- CD music from files (`MUSIC/TrackNN.ogg|mp3|wav`) including in-game music selection
- Settings menu in English and German (follows the system language)
- Uses kubridge for a fast dynarec; works with vita-turbo-storage

## Requirements

**PS Vita**
- PS Vita or PS TV on firmware 3.60–3.74 with HENkaku / Ensō (or h-encore)
- [VitaShell](https://github.com/TheOfficialFloW/VitaShell) to install the VPK and copy files
- [kubridge](https://github.com/bythos14/kubridge) **0.3.1 or newer** in `ur0:tai/config.txt` under `*KERNEL` (required)
- Recommended: vita-turbo-storage, plus about 400 MB free on `ux0:` for the game files

**Game**
- Your own PC installation of **Civilization II: Multiplayer Gold Edition** (tested: 5.4.0f German; the English edition should work)
- Civ2UIALauncher (Civ2UIA): You must install and run the Civ2UIALauncher on your PC installation of Civilization II before building/transferring the files. This patches the game's executable and UI to enable proper interface and control support on the PS Vita.
- Optional: the original game CD to rip the music tracks

**Native version (patcher, optional)**
- Windows 10/11 64-bit, at least 4 GB RAM, about 1 GB free disk space
- Internet on first run (Python ~11 MB and VitaSDK ~150 MB are downloaded)
- **Windows 11 Smart App Control must be turned off** (see below)

## Installation

1. Install kubridge 0.3.1+ (add it under `*KERNEL` in `ur0:tai/config.txt`) and reboot the Vita.
2. Install `C2-Vita_1.0.vpk` with VitaShell.
3. Copy your **complete** installed game folder to `ux0:data/civ2/`
   (`civ2.exe`, all `.dll`, `.txt`, `.gif`, `.sav` files and the `Sound` folder).
4. Optional, CD music: rip the game CD audio tracks and copy them as
   `ux0:data/civ2/MUSIC/Track02.ogg`, `Track03.ogg`, ... (`.ogg`, `.mp3` or `.wav`; track 1 is the data track).
5. Start **C2-Vita** from the LiveArea. The public VPK shows a short "Emulation mode" notice on start.

### Native version (recommended)

1. Download `C2-Vita-Patcher_1.0.zip` and extract it.
2. Double-click `C2-Vita-Patcher.bat` (or drag your `civ2.exe` onto it) and select your `civ2.exe`.
3. Wait 10–30 minutes. On the first run Python (~11 MB) and VitaSDK (~150 MB) are downloaded and cached.
4. Install the generated `C2-Vita_1.0_nativ.vpk` with VitaShell. It replaces the public VPK, and your saves are kept.

The generated VPK contains code derived from **your** `civ2.exe`. It is for your
own use only. **Do not share it.**

### ⚠️ Windows Requirements & Troubleshooting

When downloading and extracting the VitaSDK automatically, standard Windows tools may fail with the following error:
`tar.exe: Error opening archive: Can't initialize filter; unable to run program "bzip2 -d"`[cite: 1]

This happens because standard Windows CMD includes `tar` but lacks the `bzip2` decompression tool[cite: 1].

**How to resolve:**
1. **Install Git for Windows (Recommended):** During setup, make sure to check **"Use Git and optional Unix tools from the Command Prompt"**. This adds `bzip2` system-wide.
2. **Manual Extraction Alternative:** 
   - Download the VitaSDK file directly from the link shown in the patcher terminal[cite: 1].
   - Extract it using **7-Zip** or **WinRAR**.
   - Place the extracted `vitasdk` directory directly into the patcher root folder before running the tool.

### Smart App Control (Windows 11)

Smart App Control blocks the unsigned VitaSDK tools. When it is on, the patcher
fails at *Building the VPK* with "An Application Control policy has blocked this
file".

- **Turn it off:** Windows Security → App & browser control → Smart App Control → Settings → **Off**.
- **Turn it back on afterwards, without reinstalling Windows** (unofficial registry tweak, at your own risk):
  open Command Prompt as administrator and run

  ```bat
  reg add "HKLM\SYSTEM\CurrentControlSet\Control\CI\Policy" /v VerifiedAndReputablePolicyState /t REG_DWORD /d 1 /f
  ```

  then restart the PC. Value `1` = on, `2` = evaluation mode, `0` = off.

## Controls (default)

| Button | Action |
|---|---|
| Left / right stick | Mouse pointer (right stick is slower, for precision) |
| D-pad | Directions (move units, 8-way) |
| L / R | Left / right mouse button |
| Cross | Irrigate (I) |
| Circle | Fortify (F) |
| Square | Build road (R) |
| Triangle | Build city (B) |
| Start | Enter |
| Select | Quick-command menu (end turn, save, on-screen keyboard, settings, screenshot, ...) |
| Start + Select | Settings |
| Touchscreen | Tap = left click |

Everything can be remapped in the settings menu. The settings are stored in
`ux0:data/civ2/steuerung.ini`.

## Troubleshooting

- **Safe mode:** hold **L + R** while the game starts. This disables native code for this session.
- Logs are written to `ux0:data/civ2/civ2vita.log` and `winx86.log`. Please attach both when you report an issue.
- **Screenshot:** Select, then *Screenshot*. This saves `ux0:data/civ2/bildschirmfoto_N.bmp`.
- **Old LiveArea images** after an update: reinstall the VPK or use *Rebuild Database*.
- **Game does not start or crashes right away:** check that kubridge 0.3.1+ is installed and that `ux0:data/civ2/civ2.exe` exists.
- **Patcher fails at "Building the VPK":** turn off Smart App Control (see above).
- **Settings show "Not installed – emulation only":** this is the public VPK. Use the patcher for the native version.

## Known limitations

- Animated diplomats and videos (intro, wonders, spaceship) are not supported yet. They are stored on the original CD, not in the installed game.
- Multiplayer is disabled.
- Only MGE 5.4.0f has been tested. Other versions may work; the patcher warns you about unknown versions.

## Building from source

See [docs/BUILDING.md](docs/BUILDING.md).

## How it works

- **winx86 / Box86 dynarec** executes x86 code on the Vita's ARM CPU.
- C2-Vita re-implements the needed parts of the Win32 API (window manager, GDI, WinMM audio, CD audio via MCI, file dialogs, and more) on top of the Vita SDK.
- **`tools/recomp.py`** is a static recompiler. It turns the game's x86 functions into C code for the Vita, using its own built-in disassembler (`tools/x86dis.py`), and keeps x87 FPU state shared with the emulator. It only ever runs on the user's own copy of the game, via the patcher.

## Community & Support
Want to share feedback, report bugs, suggest new features, or just chat with other users? Join our community!

👉 **[Join the GameNexum Discord Server](https://discord.gg/xkSFYMCgHH)**

## License

C2-Vita's own code is released under the [MIT License](LICENSE).
Third-party components are listed in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

This repository does **not** contain any part of Civilization II: no game data,
no executable code, and no code generated from it. Please do not open issues or
pull requests that contain such material.

## 🤖 Acknowledgments & Credits

* **Porting & Code Generation:** This port was fully developed and generated with the assistance of **Claude Code** (Anthropic's AI coding assistant).
* **Target Platform:** Sony PlayStation Vita / PS TV.
* **Special Thanks:** The PS Vita homebrew community for their open-source toolchains and documentation.
