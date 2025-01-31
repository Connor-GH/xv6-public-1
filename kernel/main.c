#include <stdint.h>
#include <string.h>
#include <stdint.h>
#include "drivers/memlayout.h"
#include "drivers/acpi.h"
#include "drivers/lapic.h"
#include "drivers/ps2mouse.h"
#include "param.h"
#include "proc.h"
#include "x86.h"
#include "null.h"
#include "console.h"
#include "kalloc.h"
#include "mp.h"
#include "ioapic.h"
#include "uart.h"
#include "bio.h"
#include "file.h"
#include "ide.h"
#include "vm.h"
#include "picirq.h"
#include "trap.h"
#include "boot/multiboot2.h"
#include "autogenerated/compiler_information.h"
#include "kernel_assert.h"

static void
startothers(void);
static void
mpmain(void) __attribute__((noreturn));
extern uintptr_t *kpgdir;
extern char end[]; // first address after kernel loaded from ELF file

uint64_t available_memory;
uint64_t top_memory;

extern void
rust_hello_world(void);
extern void
pci_init(void);

int
main(struct multiboot_info *mbinfo)
{
	uartinit1(); // serial port
	parse_multiboot(mbinfo);
	kernel_assert(available_memory != 0);
	kinit1(end, P2V(4 * 1024 * 1024)); // phys page allocator

	// We can start printing to the screen here.
	// Any printing before this MUST be uart.
	kvmalloc(); // kernel page table
	ioapicinit(); // another interrupt controller
	uartinit2();
	cprintf("xv6_64 (built with %s and linker %s)\n", XV6_COMPILER, XV6_LINKER);
	if (acpiinit() != 0)
		mpinit(); // detect other processors
	lapicinit(); // interrupt controller
	seginit(); // segment descriptors
	picinit(); // disable pic
	// /dev/console, not to be confused with VGA memory
	consoleinit(); // console hardware
	nulldrvinit();
	pinit(); // process table
	tvinit(); // trap vectors
	binit(); // buffer cache
	fileinit(); // file table
	ideinit(); // disk
	ps2mouseinit();
	//timerinit();
	rust_hello_world();
	pci_init();
	startothers(); // start other processors
	kinit2(P2V(4 * 1024 * 1024), P2V(PHYSTOP)); // must come after startothers()
	userinit(); // first user process
	mpmain(); // finish this processor's setup
}

// Other CPUs jump here from entryother.S.
void
mpenter(void)
{
	switchkvm();
	seginit();
	lapicinit();
	mpmain();
}

// Common CPU setup code.
static void
mpmain(void)
{
	cprintf("\033[97;44mcpu%d: starting\033[m\n", my_cpu_id());
	idtinit(); // load idt register
	xchg(&(mycpu()->started), 1); // tell startothers() we're up
	scheduler(); // start running processes
}

void
entry32mp(void);

// Start the non-boot (AP) processors.
static void
startothers(void)
{
	/* very strange:
  * the value of this var is determined by the linker. It needs to be the full directory.
  * _binary_[file_path]_start
  * */
	extern uint8_t _binary_bin_entryother_start[], _binary_bin_entryother_size[];
	uint8_t *code;
	struct cpu *c;
	char *stack;

	// Write entry code to unused memory at 0x7000.
	// The linker has placed the image of entryother.S in
	// _binary_entryother_start.
	code = p2v(0x7000);
	memmove(code, _binary_bin_entryother_start,
					(uintptr_t)_binary_bin_entryother_size);

	for (c = cpus; c < cpus + ncpu; c++) {
		if (c == mycpu()) // We've started already.
			continue;

		// Tell entryother.S what stack to use, where to enter, and what
		// pgdir to use. We cannot use kpgdir yet, because the AP processor
		// is running in low  memory, so we use entrypgdir for the APs too.
		stack = kpage_alloc();
#if X86_64
		*(uint32_t *)(code - 4) =
			0x8000; // just enough stack to get us to entry64mp
		*(uint32_t *)(code - 8) = v2p(entry32mp);
		*(uint64_t *)(code - 16) = (uint64_t)(stack + KSTACKSIZE);
#endif
		lapicstartap(c->apicid, v2p(code));

		// wait for cpu to finish mpmain()
		while (c->started == 0)
			;
	}
}
