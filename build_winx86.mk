W := winx86
B86 := $(W)/third_party/box86-dynarec
SHIM := $(W)/src/dynarec86/shim
DYN86 := $(W)/src/dynarec86
RT := $(W)/src/runtime
PLAT := $(W)/src/platform
CC := arm-vita-eabi-gcc
CXX := arm-vita-eabi-g++
AR := arm-vita-eabi-ar
O := build/wx
CFLAGS := -O2 -g -marm -march=armv7-a+simd -mfpu=neon -mfloat-abi=hard -DDYNAREC -DARM -DUSE_MMAP -DTRACE_MEMSTAT \
 -I$(SHIM)/vita -std=gnu17 -D__vita__ '-D__clear_cache(b,e)=dyn86_vita_clear_cache(b,e)' \
 -I$(SHIM) -I$(DYN86) -I$(B86)/include -I$(B86) -I$(B86)/dynarec -w
CXXFLAGS := -std=gnu++17 -O2 -g -marm -march=armv7-a+simd -mfpu=neon -mfloat-abi=hard -DD2RT_CPU_BOX86 -D__vita__ -I$(SHIM)/vita \
 -I$(W)/src -I$(DYN86) -I$(DYN86)/shim -I$(B86)/include -I$(B86) -w
CORE := $(addprefix $(B86)/,dynarec/dynarec.c dynarec/dynablock.c dynarec/dynarec_arm.c dynarec/dynarec_arm_functions.c dynarec/dynarec_arm_jmpnext.c custommem.c tools/rbtree.c emu/x87emu_private.c emu/x86primop.c emu/x86compstrings.c) \
 $(SHIM)/shim_impl.c $(SHIM)/x86run_flags.c $(DYN86)/dyn86.c $(DYN86)/alt_table.c $(DYN86)/dyn86_emitprof.c $(DYN86)/dyn86_memintrin.c $(DYN86)/dyn86_intrin.c $(SHIM)/vita/mman_vita.c $(SHIM)/vita/fault_vita.c
ASM := $(addprefix $(B86)/dynarec/,arm_prolog.S arm_epilog.S arm_next.S arm_lock_helper.S)
PASS := $(addprefix $(B86)/dynarec/,dynarec_arm_helper.c dynarec_arm_pass.c dynarec_arm_emit_tests.c dynarec_arm_emit_math.c dynarec_arm_emit_logic.c dynarec_arm_emit_shift.c dynarec_arm_00.c dynarec_arm_0f.c dynarec_arm_64.c dynarec_arm_65.c dynarec_arm_66.c dynarec_arm_67.c dynarec_arm_d8.c dynarec_arm_d9.c dynarec_arm_da.c dynarec_arm_db.c dynarec_arm_dc.c dynarec_arm_dd.c dynarec_arm_de.c dynarec_arm_df.c dynarec_arm_f0.c dynarec_arm_660f.c dynarec_arm_66f0.c dynarec_arm_f20f.c dynarec_arm_f30f.c)
RTS := $(addprefix $(RT)/,pe_image.cpp bridge.cpp cpu.cpp cpu_box86.cpp sched_cooperative.cpp gil.cpp sched_native.cpp prof.cpp audio_sink_host.cpp guest_atomics.cpp guest_files.cpp guest_toolhelp.cpp guest_sync.cpp win32_shims_sync.cpp guest_thread_ctx.cpp win32_shims_kernel32.cpp win32_shims_locale.cpp win32_shims_memory.cpp win32_shims_wait.cpp win32_shims_shell32.cpp win32_shims_advapi32.cpp win32_shims_user32.cpp win32_shims_misc.cpp win32_shims_psapi.cpp win32_shims_version.cpp authenticode.cpp win32_shims_wintrust.cpp win32_shims_gdi32.cpp win32_shims_window.cpp win32_shims_wsock32.cpp poll_gil.cpp net_nonblock.cpp net_guard.cpp guest_scratch.cpp guest_region.cpp host_clock.cpp guest_str.cpp guest_locale.cpp ds_emul.cpp) \
 $(PLAT)/vita_audio.cpp $(PLAT)/vita_host.cpp $(PLAT)/vita_gpumem.cpp $(PLAT)/present_scale.cpp $(W)/src/render/render_null.cpp
OBJS := $(patsubst %,$(O)/core/%.o,$(notdir $(CORE) $(ASM))) $(foreach s,0 1 2 3,$(patsubst %,$(O)/pass$(s)/%.o,$(notdir $(PASS)))) $(patsubst %,$(O)/rt/%.o,$(notdir $(RTS)))
vpath %.c $(sort $(dir $(CORE) $(PASS)))
vpath %.S $(B86)/dynarec
vpath %.cpp $(sort $(dir $(RTS)))
all: build/libwinx86_vita.a
build/libwinx86_vita.a: $(OBJS)
	rm -f $@; $(AR) rcs $@ $^
$(O)/core/%.c.o: %.c ; @mkdir -p $(@D); $(CC) $(CFLAGS) -c $< -o $@
$(O)/core/%.S.o: %.S ; @mkdir -p $(@D); $(CC) $(CFLAGS) -c $< -o $@
$(O)/rt/%.cpp.o: %.cpp ; @mkdir -p $(@D); $(CXX) $(CXXFLAGS) -c $< -o $@
define P
$(O)/pass$(1)/%.c.o: %.c ; @mkdir -p $$(@D); $(CC) $(CFLAGS) -DSTEP=$(1) -c $$< -o $$@
endef
$(foreach s,0 1 2 3,$(eval $(call P,$(s))))
