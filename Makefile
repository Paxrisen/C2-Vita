# civ2vita — Civilization II (MGE) fuer PS Vita
TITLE_ID := CIV200002
APP_NAME := C2-Vita
VERSION  := 01.00
CXX := arm-vita-eabi-g++
W   := winx86
CXXFLAGS := -std=gnu++17 -O2 -g -marm -march=armv7-a+simd -mfpu=neon -mfloat-abi=hard -D__vita__ -DD2RT_CPU_BOX86 \
  -I$(W)/src -I$(W)/src/dynarec86 -I$(W)/src/dynarec86/shim -I$(W)/src/dynarec86/shim/vita -I$(W)/third_party/box86-dynarec/include \
  -Wall -Wno-unused-variable -Wno-unused-parameter -Wno-unused-but-set-variable -Wno-misleading-indentation -Wno-parentheses -Wno-sign-compare -Wno-unused-function
SRCS := $(wildcard src/*.cpp)
OBJS := $(patsubst src/%.cpp,build/obj/%.o,$(SRCS))
GEN_SRCS := $(wildcard gen/native_*.c)
GEN_OBJS := $(patsubst gen/%.c,build/gen/%.o,$(GEN_SRCS))
GENFLAGS := -O2 -marm -march=armv7-a+simd -mfpu=neon -mfloat-abi=hard -Isrc -w
LIBS := build/libwinx86_vita.a -Lkustub/out -lkubridge_stub_weak \
  -lSceDisplay_stub -lSceCtrl_stub -lSceTouch_stub -lSceSysmem_stub -lSceLibKernel_stub -lSceIofilemgr_stub \
  -lSceProcessmgr_stub -lSceKernelThreadMgr_stub -lSceRtc_stub -lSceAppUtil_stub -lSceSysmodule_stub -lScePower_stub \
  -lSceAudio_stub -lSceNet_stub -lSceNetCtl_stub -lSceCommonDialog_stub -lSceVshBridge_stub \
  -Wl,-u,pthread_cancel -Wl,-u,pthread_once -lpthread -lm

all: build/civ2vita.vpk

build/obj/%.o: src/%.cpp src/rt.h src/win.h
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/gen/%.o: gen/%.c src/native_rt.h
	@mkdir -p $(@D)
	arm-vita-eabi-gcc $(GENFLAGS) -c $< -o $@

build/civ2vita.elf: $(OBJS) $(GEN_OBJS) build/libwinx86_vita.a
	$(CXX) $(CXXFLAGS) -Wl,-q $(OBJS) $(GEN_OBJS) $(LIBS) -o $@

build/eboot.bin: build/civ2vita.elf
	vita-elf-create $< build/civ2vita.velf
	vita-make-fself -s build/civ2vita.velf $@

build/param.sfo:
	vita-mksfoex -s TITLE_ID=$(TITLE_ID) -s APP_VER=$(VERSION) -d ATTRIBUTE2=12 "$(APP_NAME)" $@

build/civ2vita.vpk: build/eboot.bin build/param.sfo
	vita-pack-vpk -s build/param.sfo -b build/eboot.bin \
	  -a pkg/sce_sys/icon0.png=sce_sys/icon0.png \
	  -a pkg/sce_sys/livearea/contents/bg.png=sce_sys/livearea/contents/bg.png \
	  -a pkg/sce_sys/livearea/contents/startup.png=sce_sys/livearea/contents/startup.png \
	  -a pkg/sce_sys/livearea/contents/template.xml=sce_sys/livearea/contents/template.xml \
	  $(foreach f,$(wildcard pkg/fonts/*),-a $(f)=fonts/$(notdir $(f))) \
	  $(foreach f,$(wildcard pkg/licenses/*),-a $(f)=licenses/$(notdir $(f))) $@

# ---- oeffentliche Version: ohne uebersetzten Spielcode (nur eigener Code + Emulator)
build/pub/native_stub.o: stub/native_stub.c src/native_rt.h
	@mkdir -p $(@D)
	arm-vita-eabi-gcc $(GENFLAGS) -c $< -o $@
build/pub/c2vita.elf: $(OBJS) build/pub/native_stub.o build/libwinx86_vita.a
	$(CXX) $(CXXFLAGS) -Wl,-q $(OBJS) build/pub/native_stub.o $(LIBS) -o $@
build/pub/eboot.bin: build/pub/c2vita.elf
	vita-elf-create $< build/pub/c2vita.velf
	vita-make-fself -s build/pub/c2vita.velf $@
public: build/pub/eboot.bin build/param.sfo
	vita-pack-vpk -s build/param.sfo -b build/pub/eboot.bin \
	  -a pkg/sce_sys/icon0.png=sce_sys/icon0.png \
	  -a pkg/sce_sys/livearea/contents/bg.png=sce_sys/livearea/contents/bg.png \
	  -a pkg/sce_sys/livearea/contents/startup.png=sce_sys/livearea/contents/startup.png \
	  -a pkg/sce_sys/livearea/contents/template.xml=sce_sys/livearea/contents/template.xml \
	  $(foreach f,$(wildcard pkg/fonts/*),-a $(f)=fonts/$(notdir $(f))) \
	  $(foreach f,$(wildcard pkg/licenses/*),-a $(f)=licenses/$(notdir $(f))) build/pub/C2-Vita_1.0.vpk

clean:
	rm -rf build/obj build/*.elf build/*.velf build/eboot.bin build/param.sfo build/*.vpk
