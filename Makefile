# Cross-compiling (e.g., on Mac OS X)
# TOOLPREFIX = i386-jos-elf

# Using native tools (e.g., on X86 Linux)
#TOOLPREFIX =

# Try to infer the correct TOOLPREFIX if not set
ifndef TOOLPREFIX
TOOLPREFIX := $(shell if i386-jos-elf-objdump -i 2>&1 | grep '^elf32-i386$$' >/dev/null 2>&1; \
	then echo 'i386-jos-elf-'; \
	elif objdump -i 2>&1 | grep 'elf32-i386' >/dev/null 2>&1; \
	then echo ''; \
	else echo "***" 1>&2; \
	echo "*** Error: Couldn't find an i386-*-elf version of GCC/binutils." 1>&2; \
	echo "*** Is the directory with i386-jos-elf-gcc in your PATH?" 1>&2; \
	echo "*** If your i386-*-elf toolchain is installed with a command" 1>&2; \
	echo "*** prefix other than 'i386-jos-elf-', set your TOOLPREFIX" 1>&2; \
	echo "*** environment variable to that prefix and run 'make' again." 1>&2; \
	echo "*** To turn off this error, run 'gmake TOOLPREFIX= ...'." 1>&2; \
	echo "***" 1>&2; exit 1; fi)
endif

# If the makefile can't find QEMU, specify its path here
# QEMU = qemu-system-i386

# Try to infer the correct QEMU
ifndef QEMU
QEMU = $(shell if which qemu > /dev/null; \
	then echo qemu; exit; \
	elif which qemu-system-i386 > /dev/null; \
	then echo qemu-system-i386; exit; \
	elif which qemu-system-x86_64 > /dev/null; \
	then echo qemu-system-x86_64; exit; \
	else \
	qemu=/Applications/Q.app/Contents/MacOS/i386-softmmu.app/Contents/MacOS/i386-softmmu; \
	if test -x $$qemu; then echo $$qemu; exit; fi; fi; \
	echo "***" 1>&2; \
	echo "*** Error: Couldn't find a working QEMU executable." 1>&2; \
	echo "*** Is the directory containing the qemu binary in your PATH" 1>&2; \
	echo "*** or have you tried setting the QEMU variable in Makefile?" 1>&2; \
	echo "***" 1>&2; exit 1)
endif

# TODO get rid of this
WNOFLAGS = -Wno-error=infinite-recursion
ARCHNOFLAGS = -mno-sse -mno-red-zone -mno-avx -mno-avx2

CC = $(TOOLPREFIX)gcc
AS = $(TOOLPREFIX)gas
LD = $(TOOLPREFIX)ld
AR = $(TOOLPREFIX)ar
RANLIB = $(TOOLPREFIX)ranlib

OBJCOPY = $(TOOLPREFIX)objcopy
OBJDUMP = $(TOOLPREFIX)objdump

# set to 64 when ready
BITS = 64

# llvm stuff
ifneq ($(LLVM),)
	CC = clang
	AS = $(CC) -c
	LD = ld.lld
	OBJCOPY = llvm-objcopy
	OBJDUMP = llvm-objdump
	AR = llvm-ar
	RANLIB = llvm-ranlib
	LINKER_FLAGS = -fuse-ld=lld
endif
# Dlang toolchain
DC ?= dmd
<<<<<<< HEAD
D_PREVIEWS = -preview=systemVariables,in,dip1000,dip1021
DFLAGS ?= -betterC -O $(EXTRA_DFLAGS)
=======
D_PREVIEWS = -preview=systemVariables,in,dip1000,dip1021,nosharedaccess,fixImmutableConv
DFLAGS ?= -betterC -O -m32 $(EXTRA_DFLAGS)
>>>>>>> relics

ifeq ($(shell $(DC) --version | sed -n 1p | awk '{print $$1}'),DMD64)
	DC_type = dmd
else
	DC_type = ldc2
endif
ifneq ($(DC_type),dmd)
	DFLAGS += -defaultlib -mattr=-avx,-sse
	D_ARCHFLAG32 = -mcpu=i686
	D_ARCHFLAG64 = -mcpu=x86-64
else
	DFLAGS += -defaultlib=none
	D_ARCHFLAG32 =
	D_ARCHFLAG64 = -mcpu=baseline
endif
# we will support dmd-style D compilers only.

WFLAGS = -Wnonnull -Werror=pointer-to-int-cast -Werror=int-to-pointer-cast # -Werror=format

CFLAGS = -std=gnu11 -pipe -pedantic -fno-pic -static -fno-builtin -ffreestanding \
				 -fno-strict-aliasing -nostdlib -O2 -Wall -ggdb -Wno-error -fno-omit-frame-pointer \
				 -nostdinc -fno-builtin $(ARCHNOFLAGS) $(WNOFLAGS) $(WFLAGS)
CFLAGS += $(shell $(CC) -fno-stack-protector -E -x c /dev/null >/dev/null 2>&1 && echo -fno-stack-protector)
ifneq ($(RELEASE),)
	CFLAGS += -O2
endif
ASFLAGS = -gdwarf-2 -Wa,-divide --mx86-used-note=no
ifeq ($(BITS),64)
	CFLAGS += -m64 -march=x86-64 -mcmodel=kernel -mtls-direct-seg-refs -DX64=1
	ASFLAGS += -m64 -march=x86-64 -mcmodel=kernel -mtls-direct-seg-refs -DX64=1
	DFLAGS += -m64 $(D_ARCHFLAG64)
else
	CFLAGS += -m32 -march=i386
	ASFLAGS += -m32
	DFLAGS += -m32 $(D_ARCHFLAG32)
endif
# FreeBSD ld wants ``elf_i386_fbsd''
ifeq ($(HOST_OS),FreeBSD)
	ifeq ($(BITS),64)
	LDFLAGS += -m $(shell $(LD) -V | grep elf_x86_64 2>/dev/null | head -n 1)
else
	LDFLAGS += -m $(shell $(LD) -V | grep elf_i386 2>/dev/null | head -n 1)
endif
else
	ifeq ($(BITS),64)
	LDFLAGS += -melf_x86_64
else
	LDFLAGS += -m elf_i386
endif
endif
LDFLAGS += -z noexecstack -O1


# Disable PIE when possible (for Ubuntu 16.10 toolchain)
ifneq ($(shell $(CC) -dumpspecs 2>/dev/null | grep -e '[^f]no-pie'),)
CFLAGS += -fno-pie -no-pie
endif
ifneq ($(shell $(CC) -dumpspecs 2>/dev/null | grep -e '[^f]nopie'),)
CFLAGS += -fno-pie -nopie
endif


CLEAN := $(filter clean, $(MAKECMDGOALS))

IVARS = -Iinclude/ -I.
# directories
TOOLSDIR = tools
BIN = bin
SYSROOT = sysroot

DIRECTORIES = $(BIN) $(SYSROOT)/$(BIN) $(BIN)/64 $(BIN)/32

default: $(CLEAN)
	$(MAKE) $(KERNELDIR)/include/autogenerated/compiler_information.h
	$(MAKE) $(DIRECTORIES)
	$(MAKE) images
images: $(BIN)/fs.img $(BIN)/xv6.img
$(DIRECTORIES):
	mkdir -p $@

include d/Makefile
include userspace/Makefile
include kernel/Makefile



$(KERNELDIR)/include/autogenerated/compiler_information.h:
	mkdir -p $(KERNELDIR)/include/autogenerated
	printf \
	"#pragma once\n \
	#define XV6_COMPILER \"$(shell $(CC) --version | sed -n 1p)\"\n \
	#define XV6_LINKER \"$(shell $(LD) --version | sed -n 1p)\"\n" \
		> $(KERNELDIR)/include/autogenerated/compiler_information.h


$(BIN)/mkfs: $(TOOLSDIR)/mkfs.c
	$(CC) $(LINKER_FLAGS) -Werror -Wall -o $@ $^ \
		-I$(KERNELDIR)/include -I$(KERNELDIR)/drivers/include -I$(KERNELDIR)/drivers -I.

# Prevent deletion of intermediate files, e.g. cat.o, after first build, so
# that disk image changes after first build are persistent until clean.  More
# details:
# http://www.gnu.org/software/make/manual/html_node/Chained-Rules.html
.PRECIOUS: $(BIN)/%.o


$(BIN)/fs.img: $(BIN)/mkfs $(UPROGS) $(D_PROGS)
	./$(BIN)/mkfs $@ README.md sysroot/test.sh sysroot/etc/passwd $(UPROGS) $(D_PROGS)

clean:
	@if [ -z "$(BIN)" ]; then exit 1; fi
	@if [ "x$(SYSROOT)" = "x" ]; then exit 1; fi
	@if [ "x$(KERNELDIR)" = "x" ]; then exit 1; fi
	@if [ "x$(BIN)" = "x$$HOME" ]; then exit 1; fi

	rm -rf $(BIN)/*.o $(BIN)/*.sym $(BIN)/bootblock $(BIN)/entryother \
	$(SYSROOT)/bin/* \
	$(SYSROOT)/mkfs \
	$(BIN)/initcode \
	$(BIN)/initcode.out \
	$(BIN)/kernel \
	$(BIN)/xv6.img \
	$(BIN)/fs.img \
	$(BIN)/kernelmemfs \
	$(KERNELDIR)/include/autogenerated/* \
	$(BIN)/xv6memfs.img mkfs $(BIN)/*

# run in emulators

bochs : fs.img xv6.img
	if [ ! -e .bochsrc ]; then ln -s dot-bochsrc .bochsrc; fi
	bochs -q

# try to generate a unique GDB port
GDBPORT = $(shell expr `id -u` % 5000 + 25000)
# QEMU's gdb stub command line changed in 0.11
QEMUGDB = $(shell if $(QEMU) -help | grep -q '^-gdb'; \
	then echo "-gdb tcp::$(GDBPORT)"; \
	else echo "-s -p $(GDBPORT)"; fi)
ifndef CPUS
CPUS := 2
endif
ifndef MEM
MEM := 224M
endif
QEMUOPTS = -kernel $(BIN)/kernel -drive file=$(BIN)/fs.img,index=1,media=disk,format=raw,if=ide,aio=native,cache.direct=on \
					 -smp cpus=$(CPUS),cores=1,threads=1,sockets=$(CPUS) -m $(MEM) $(QEMUEXTRA)

ifdef CONSOLE_LOG
	QEMUOPTS += -serial mon:stdio
endif
qemu: default
	$(QEMU) $(QEMUOPTS)

qemu-memfs: xv6memfs.img
	$(QEMU) -drive file=$(BIN)/xv6memfs.img,index=0,media=disk,format=raw -smp $(CPUS) -m $(MEM)

qemu-nox: default
	$(QEMU) -nographic $(QEMUOPTS)

.gdbinit: debug/.gdbinit.tmpl
	sed "s/localhost:1234/localhost:$(GDBPORT)/" < $^ > $@

qemu-gdb: default
	$(QEMU) $(QEMUOPTS) -S $(QEMUGDB)
	@echo "*** Now run 'gdb'." 1>&2

qemu-nox-gdb: fs.img xv6.img .gdbinit
	@echo "*** Now run 'gdb'." 1>&2
	$(QEMU) -nographic $(QEMUOPTS) -S $(QEMUGDB)

format:
	@find . -iname *.h -o -iname *.c | xargs clang-format -style=file:.clang-format -i
