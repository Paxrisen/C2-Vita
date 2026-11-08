# C2-Vita 1.0

First public release of **C2-Vita**, an unofficial PS Vita runtime for
*Civilization II: Multiplayer Gold Edition* (PC).

⚠️ This release contains **no game files**. You need your own PC copy of the game.

## Downloads

| File | Description |
|---|---|
| `C2-Vita_1.0.vpk` | Install with VitaShell. The game code runs in the built-in x86 emulator. |
| `C2-Vita-Patcher_1.0.zip` | Windows tool. It builds a **native** VPK from your own `civ2.exe` (faster). Do not share the resulting VPK. |

## Quick start

1. Install the kubridge plugin (0.3.1 or newer).
2. Install `C2-Vita_1.0.vpk`.
3. Copy your installed game folder to `ux0:data/civ2/`.
4. Optional: run the patcher on your PC and install the generated native VPK.
   Windows 11: Smart App Control must be turned off for the patcher (see README for how to turn it back on).

See the README for controls, CD music and troubleshooting.

## Known limitations

- No videos or animated diplomats yet (they are stored on the game CD)
- No multiplayer
- Tested with MGE 5.4.0f (German)
