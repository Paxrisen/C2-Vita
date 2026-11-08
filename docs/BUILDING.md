# Building C2-Vita

## Requirements

- Linux (or WSL) with [vitasdk](https://vitasdk.org) installed and `VITASDK` set
- Python 3.8+
- About 4 GB RAM per parallel compiler job for the native build

## 1. Build the winx86 runtime library

```sh
make -f build_winx86.mk -j4
```

## 2a. Public VPK (no game code, emulator only)

```sh
make public
# -> build/pub/C2-Vita_1.0.vpk
```

## 2b. Native VPK from your own civ2.exe (personal use only)

```sh
python3 tools/recomp.py - /path/to/your/civ2.exe gen   # '-' = built-in disassembler
make -j2
# -> build/civ2vita.vpk
```

`gen/` holds code derived from your game executable. It is listed in
`.gitignore`; never commit or distribute it.

## Patcher (Windows)

`patcher/` contains the Windows front-end (`C2-Vita-Patcher.bat`, `data/patcher.ps1`).
The release ZIP additionally contains:

- `data/recomp.py`, `data/x86dis.py`, `data/imp_argc.json`, `data/native_rt.h` (copied from `tools/` and `src/`)
- `data/obj/c2_pre.o`, `c2_main.o`, `c2_post.o`: the C2-Vita runtime, pre-linked with `ld -r`
  (crt files, C2-Vita objects, `libwinx86_vita.a`, vitasdk libraries; contains no game code)
- `data/pkg/`: LiveArea images, fonts and licenses

Pre-link example (run after the normal build):

```sh
L=$VITASDK/lib/gcc/arm-vita-eabi/<gcc-version>; A=$VITASDK/arm-vita-eabi/lib
arm-vita-eabi-ld -r -o c2_pre.o  $L/crti.o $L/crtbegin.o $A/crt0.o
arm-vita-eabi-ld -r -o c2_post.o $L/crtend.o $L/crtn.o
arm-vita-eabi-ld -r -o c2_main.o $(sed 's/^/-u /' undef.txt) -u pthread_cancel -u pthread_once \
  -L$A -L$L -Lkustub/out build/obj/*.o build/libwinx86_vita.a <SCE stub libs> \
  -lpthread -lstdc++ -lm --start-group -lgcc -lc <SCE stub libs> --end-group
```

`undef.txt` lists the symbols that the generated code and the crt files import.
Get it with `arm-vita-eabi-nm -u` on `build/gen/*.o` and the crt files, minus
the symbols defined in `build/gen/*.o`.

## Verifying the recompiler

`tools/difftest/` is a differential test harness. It runs the original and the
recompiled function on an x86-64 Linux host and compares registers and memory.
It needs your own `civ2.exe`; see the comments in `prep.py`.
