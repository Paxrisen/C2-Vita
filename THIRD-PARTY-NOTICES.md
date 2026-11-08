# Third-party notices

| Component | Author | License | Location |
|---|---|---|---|
| winx86 (Win32-on-Vita runtime) | Franck Roustan | MIT | `winx86/` (see `winx86/LICENSE`, `winx86/THIRD-PARTY-NOTICES.md`) |
| Box86 dynarec core | Sebastien Chevalier ("ptitSeb") | MIT | `winx86/third_party/box86-dynarec/` |
| stb_vorbis | Sean Barrett | Public domain / MIT | `src/third/stb_vorbis.c` |
| dr_mp3 | David Reid | Public domain / MIT-0 | `src/third/dr_mp3.h` |
| IBM Plex Serif / Mono fonts | IBM Corp. | SIL OFL 1.1 | `pkg/fonts/` |
| Work Sans font | Wei Huang | SIL OFL 1.1 | `pkg/fonts/` |
| Gloock font (used to draw the LiveArea images) | Duarte Pinto | SIL OFL 1.1 | images in `pkg/sce_sys/` |
| kubridge stubs | generated from `kustub/stubs.yml` | – | `kustub/` |
| vitasdk libraries (newlib, libstdc++, pthread-embedded, SCE stubs) | vitasdk contributors | various free licenses | linked into the VPK |

The patcher downloads at runtime (they are not bundled):
- Python embeddable package: Python Software Foundation License
- VitaSDK for Windows: various free licenses (GPL toolchain, see vitasdk)

The LiveArea artwork in `pkg/sce_sys/` is original work created for this project
(`tools/make_livearea_art.py`). It is MIT-licensed like the rest of the code.
