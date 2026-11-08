ifdef VITASDK
PREFIX = $(VITASDK)/bin/
endif

ARCH ?= $(PREFIX)arm-vita-eabi
AS = $(ARCH)-as
AR = $(ARCH)-ar
RANLIB = $(ARCH)-ranlib

TARGETS = libkubridge_stub.a
TARGETS_WEAK = libkubridge_stub_weak.a

kubridge_OBJS = kubridge_kubridge_kuKernelFlushCaches.o kubridge_kubridge_kuKernelMemReserve.o kubridge_kubridge_kuKernelMemCommit.o kubridge_kubridge_kuKernelMemDecommit.o kubridge_kubridge_kuKernelMemProtect.o kubridge_kubridge_kuKernelRegisterExceptionHandler.o kubridge_kubridge_kuKernelReleaseExceptionHandler.o
kubridge_weak_OBJS = kubridge_kubridge_kuKernelFlushCaches.wo kubridge_kubridge_kuKernelMemReserve.wo kubridge_kubridge_kuKernelMemCommit.wo kubridge_kubridge_kuKernelMemDecommit.wo kubridge_kubridge_kuKernelMemProtect.wo kubridge_kubridge_kuKernelRegisterExceptionHandler.wo kubridge_kubridge_kuKernelReleaseExceptionHandler.wo
SceKernel_OBJS =
ALL_OBJS=

all: $(TARGETS) $(TARGETS_WEAK)

define LIBRARY_template
 $(1): $$($(1:lib%_stub.a=%)_OBJS)
 ALL_OBJS += $$($(1:lib%_stub.a=%)_OBJS)
endef
define LIBRARY_WEAK_template
 $(1): $$($(1:lib%_stub_weak.a=%)_weak_OBJS)
 ALL_OBJS += $$($(1:lib%_stub_weak.a=%)_weak_OBJS)
endef

$(foreach library,$(TARGETS),$(eval $(call LIBRARY_template,$(library))))
$(foreach library,$(TARGETS_WEAK),$(eval $(call LIBRARY_WEAK_template,$(library))))

install: $(TARGETS) $(TARGETS_WEAK)
	cp $(TARGETS) $(VITASDK)/arm-vita-eabi/lib
	cp $(TARGETS_WEAK) $(VITASDK)/arm-vita-eabi/lib

clean:
	rm -f $(TARGETS) $(TARGETS_WEAK) $(ALL_OBJS)

$(TARGETS) $(TARGETS_WEAK):
	@echo "$?" > $@-objs
	$(AR) cru $@ @$@-objs
	$(RANLIB) $@

%.o: %.S
	$(AS) --defsym GEN_WEAK_EXPORTS=0 $< -o $@

%.wo: %.S
	$(AS) --defsym GEN_WEAK_EXPORTS=1 $< -o $@
