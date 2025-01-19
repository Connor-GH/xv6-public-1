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

QEMU = qemu-system-x86_64

ARCHNOFLAGS = -mno-sse -mno-red-zone -mno-avx -mno-avx2

CC = $(TOOLPREFIX)gcc
AS = $(TOOLPREFIX)gas
LD = $(TOOLPREFIX)ld
AR = $(TOOLPREFIX)ar
RANLIB = $(TOOLPREFIX)ranlib

OBJCOPY = $(TOOLPREFIX)objcopy
OBJDUMP = $(TOOLPREFIX)objdump
CARGO = $(TOOLPREFIX)cargo +nightly

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
# we will support dmd-style D compilers only.

WFLAGS = -Wall -Wextra -Wformat -Wnull-dereference -Warray-bounds -Wswitch -Wshadow
# TODO get rid of this
WNOFLAGS = -Wno-unused-parameter -Wno-infinite-recursion -Wno-pointer-arith -Wno-unused -Wno-pedantic -Wno-sign-compare

CFLAGS = -std=gnu11 -pipe -fno-pic -static -fno-builtin -ffreestanding \
				 -fno-strict-aliasing -nostdlib -O0 -ggdb -fno-omit-frame-pointer \
				 -nostdinc -fno-builtin $(ARCHNOFLAGS) $(WFLAGS) $(WNOFLAGS) $(WNOGCC)
CFLAGS += $(shell $(CC) -fno-stack-protector -E -x c /dev/null >/dev/null 2>&1 && echo -fno-stack-protector)
RUSTFLAGS = -Ctarget-feature=-avx,-sse -Crelocation-model=static -Cpanic=abort -Copt-level=0 -Ccode-model=kernel -Cno-redzone -Cincremental=true -Zthreads=4
ifneq ($(RELEASE),)
	CFLAGS += -O2
	CARGO_RELEASE = --release
	RUSTFLAGS += -C opt-level=2
	KCFLAGS += -D__KERNEL_DEBUG__=0
else
	CFLAGS += -Werror
	KCFLAGS += -D__KERNEL_DEBUG__=1
endif
ASFLAGS = -gdwarf-2 -Wa,-divide --mx86-used-note=no
ifeq ($(BITS),64)
	CFLAGS += -m64 -march=x86-64 -mcmodel=kernel -mtls-direct-seg-refs -DX86_64=1
	ASFLAGS += -m64 -march=x86-64 -mcmodel=kernel -mtls-direct-seg-refs -DX86_64=1
else
	CFLAGS += -m32 -march=i386
	ASFLAGS += -m32
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

DIRECTORIES = $(BIN) $(SYSROOT)/$(BIN) $(BIN)/64

default: $(CLEAN)
	$(MAKE) $(KERNELDIR)/include/autogenerated/compiler_information.h
	$(MAKE) $(DIRECTORIES)
	$(MAKE) images
images: $(BIN)/fs.img $(BIN)/kernel
$(DIRECTORIES):
	mkdir -p $@

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
	./$(BIN)/mkfs $@ README.md sysroot/test.sh $(wildcard sysroot/etc/*) $(UPROGS) $(D_PROGS)

clean: cargo_clean
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
	$(BIN)/xv6memfs.img mkfs $(BIN)/* \
	iso/kernel

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
QEMUOPTS = -drive file=$(BIN)/fs.img,index=1,media=disk,format=raw,if=ide,aio=native,cache.direct=on \
					 -enable-kvm -smp cpus=$(CPUS),cores=1,threads=1,sockets=$(CPUS) -m $(MEM) $(QEMUEXTRA)

ifdef CONSOLE_LOG
	QEMUOPTS += -serial mon:stdio
endif
# qemu-system-x86_64 -cdrom os.iso -m 2G -drive file=bin/fs.img,index=1,media=disk,format=raw,if=ide,aio=native,cache.direct=on  -smp 2,cores=1,threads=1,sockets=2
ISO = xv6
iso: default
	cp $(BIN)/kernel iso/boot/
	grub-mkrescue -o $(BIN)/$(ISO).iso iso/

qemu-grub: iso
	$(QEMU) -cdrom $(BIN)/$(ISO).iso $(QEMUOPTS)

qemu: qemu-grub

qemu-gdb: iso
	$(QEMU) -cdrom $(BIN)/$(ISO).iso $(QEMUOPTS) -S $(QEMUGDB)
	@echo "*** Now run 'gdb'." 1>&2

qemu-memfs: xv6memfs.img
	$(QEMU) -drive file=$(BIN)/xv6memfs.img,index=0,media=disk,format=raw -smp $(CPUS) -m $(MEM)

.gdbinit: debug/.gdbinit.tmpl
	sed "s/localhost:1234/localhost:$(GDBPORT)/" < $^ > $@

format:
	@find . -iname *.h -o -iname *.c | xargs clang-format -style=file:.clang-format -i
