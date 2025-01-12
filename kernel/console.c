// Console input and output.
// Input is from the keyboard or serial port.
// Output is written to the screen and serial port.

#include <stdint.h>
#include <stdint.h>
#include <stdarg.h>
#include "console.h"
#include "file.h"
#include "ioapic.h"
#include "kalloc.h"
#include "kernel_string.h"
#include "boot/multiboot2.h"
#include "proc.h"
#include "spinlock.h"
#include "traps.h"
#include "uart.h"
#include "x86.h"
#include "drivers/memlayout.h"
#include "drivers/conscolor.h"
#include "drivers/lapic.h"
#include "compiler_attributes.h"

static int panicked = 0;
int echo_out = 1;
static uint8_t static_foreg = WHITE;
static uint8_t static_backg = BLACK;
static int alt_form = 0;
static int long_form = 0;
static int zero_form = 0;

/*
 * This resource protects any static variable in this file, but mainly:
 * - console_buffer
 * - buffer_position
 * - crt
 */
static struct {
	struct spinlock lock;
	int locking;
} cons;

// color is default if it is set to 0xff
static void
set_term_color(uint8_t foreground, uint8_t background)
{
	static_foreg = (foreground == 0xff) ? static_foreg : foreground;
	static_backg = (background == 0xff) ? static_backg : background;
}

static void
printint(uint64_t xx, int base, int sign, int *padding)
{
	static char digits[] = "0123456789abcdef";
	char buf[32];
	int i;
	uint64_t x;
	long_form = 0;

	// the second check prevents misusage of the function.
	// we expect sign && xx < 0, but plan for the worst.
	if (sign && (sign = (xx < 0)))
		x = -xx;
	else
		x = xx;

	i = 0;
	do {
		buf[i++] = digits[x % base];
	} while ((x /= base) != 0);
	while (i < *padding) {
		buf[i++] = '0';
	}
	*padding = 0;

	if (sign)
		buf[i++] = '-';

	if (alt_form && base == 16) {
		consputc('0');
		consputc('x');
		alt_form = 0;
	}
	while (--i >= 0)
		consputc(buf[i]);
}

// Print to the console. only understands %d, %x, %p, %s.
__attribute__((format(printf, 1, 2))) __nonnull(1) void cprintf(const char *fmt,
																																...)
{
	int i, c, locking;
	va_list argp;
	char *s;
	int padding = 0;

	va_start(argp, fmt);

	locking = cons.locking;
	if (locking)
		acquire(&cons.lock);

	for (i = 0; (c = fmt[i] & 0xff) != 0; i++) {
		if (c != '%') {
			// \ef1 gives foreground color 1, which is blue.
			if (c == '\e') {
				c = fmt[++i] & 0xff;
				if (c == 0)
					break;
				switch (c) {
				case 'f': {
					c = fmt[++i] & 0xff;
					if (c == 0)
						break;
					// if [0..10), use color_below10. else, try for hex to int.
					// TODO make this easier to read with atoi or similar.
					_Bool color_below10 = c - 0x30 <= 0xf;
					if (color_below10 ||
							(((c - 0x61) + 10) >= 0xa && ((c - 0x61) + 0xa) <= 0xf)) {
						set_term_color(c - (color_below10 ? 30 : 60), 0xff);
						goto skip_printing;
					}
					break;
				}
				case 'b': {
					c = fmt[++i] & 0xff;
					if (c == 0)
						break;
					_Bool color_below10 = c - 0x30 <= 0xf;
					if (color_below10 ||
							(((c - 0x61) + 0xa) >= 0xa && ((c - 0x61) + 0xa) <= 0xf)) {
						set_term_color(0xff, c - (color_below10 ? 0x30 : 60));
						goto skip_printing;
					}
					break;
				}
				}
			}
			consputc(c);
			continue;
		}
do_again:
		c = fmt[++i] & 0xff;
		if (c == 0)
			break;
		switch (c) {
		case 'u':
			if (long_form == 0) {
				printint(va_arg(argp, unsigned int), 10, 0, &padding);
			} else {
				printint(va_arg(argp, unsigned long), 10, 0, &padding);
			}
			break;
		case 'd':
			if (long_form == 0) {
				printint(va_arg(argp, int), 10, 1, &padding);
			} else {
				printint(va_arg(argp, long), 10, 1, &padding);
			}
			break;
		case '0':
			zero_form = 1;
			padding = 18; // TODO make dynamic
			goto do_again;
		case '#':
			alt_form = 1;
			goto do_again;
		case 'l':
			long_form = 1;
			goto do_again;
		case 'x':
			if (long_form == 0)
				printint(va_arg(argp, unsigned int), 16, 0, &padding);
			else
				printint(va_arg(argp, unsigned long), 16, 0, &padding);
			break;
		case 'p':
			alt_form = 1;
			printint((uintptr_t)va_arg(argp, void *), 16, 0, &padding);
			break;
		case 'o':
			if (long_form == 0)
				printint(va_arg(argp, unsigned int), 8, 0, &padding);
			else
				printint(va_arg(argp, unsigned long), 8, 0, &padding);
			break;
		case 's':
			if ((s = va_arg(argp, char *)) == 0)
				s = "(null)";
			for (; *s; s++)
				consputc(*s);
			break;
		case '%':
			consputc('%');
			break;
		default:
			// Print unknown % sequence to draw attention.
			consputc('%');
			consputc(c);
			break;
		}
skip_printing:;
	}

	if (locking)
		release(&cons.lock);
	va_end(argp);
}

__noreturn __cold void
panic(const char *s)
{
	int i;
	uintptr_t pcs[10];

	cli();
	cons.locking = 0;
	// use lapiccpunum so that we can call panic from mycpu()
	cprintf("lapicid %d: panic: ", lapicid());
	cprintf("%s", s);
	cprintf("\n");
	getcallerpcs(&s, pcs);
	for (i = 0; i < 10; i++)
		cprintf(" %#lx", pcs[i]);
	panicked = 1; // freeze other CPU
	for (;;)
		;
}

#define BACKSPACE 0x100
#define CRTPORT 0x3d4
static uint16_t *crt = (uint16_t *)P2V(0xb8000); // CGA memory

// Cursor position: col + 80*row.
static int
cursor_position(void)
{
	int pos;
	outb(CRTPORT, 14);
	pos = inb(CRTPORT + 1) << 8;
	outb(CRTPORT, 15);
	pos |= inb(CRTPORT + 1);
	return pos;
}

static void
cgaputc(int c, uint8_t fore, uint8_t back)
{
	int pos = cursor_position();

	if (c == '\n') {
		if (pos % 80 != 0)
			pos += 80 - pos % 80;
	} else if (c == BACKSPACE && echo_out == 1) {
		if (pos > 0)
			--pos;
	} else if (echo_out == 1) {
		uint8_t higher = back;
		uint16_t together = 0; /* vga memory */
		higher <<= 4;
		higher |= fore;
		together = higher;
		together <<= 8;
		crt[pos++] = (c & 0xff) | together;
	}

	// silently ignore possible out of bounds for pos
	if (pos < 0 || pos > 25 * 80)
		pos = 0;

	if ((pos / 80) >= 24) { // Scroll up.
		memmove(crt, crt + 80, sizeof(crt[0]) * 23 * 80);
		pos -= 80;
		memset(crt + pos, 0, sizeof(crt[0]) * (24 * 80 - pos));
	}

	outb(CRTPORT, 14);
	outb(CRTPORT + 1, pos >> 8);
	outb(CRTPORT, 15);
	outb(CRTPORT + 1, pos);
	crt[pos] = ' ' | 0x0700;
}

void
consputc(int c)
{
	if (panicked) {
		cli();
		for (;;)
			;
	}

	if (c == BACKSPACE) {
		uartputc('\b');
		uartputc(' ');
		uartputc('\b');
	} else
		uartputc(c);
	cgaputc(c, static_foreg, static_backg);
}

#define INPUT_BUF 128
struct {
	char buf[INPUT_BUF];
	uint32_t r; // Read index
	uint32_t w; // Write index
	uint32_t e; // Edit index
} input;

#define C(x) ((x) - '@') // Control-x

void
consoleintr(int (*getc)(void))
{
	int c, doprocdump = 0;

	acquire(&cons.lock);
	while ((c = getc()) >= 0) {
		switch (c) {
		case C('P'): // Process listing.
			// procdump() locks cons.lock indirectly; invoke later
			doprocdump = 1;
			break;
		case C('U'): // Kill line.
			while (input.e != input.w &&
						 input.buf[(input.e - 1) % INPUT_BUF] != '\n') {
				input.e--;
				consputc(BACKSPACE);
			}
			break;
		case C('H'):
		case '\x7f': // Backspace
			if (input.e != input.w) {
				input.e--;
				consputc(BACKSPACE);
			}
			break;
		default:
			if (c != 0 && input.e - input.r < INPUT_BUF) {
				c = (c == '\r') ? '\n' : c;
				input.buf[input.e++ % INPUT_BUF] = c;
				if (c != C('D'))
					consputc(c);
				if (c == '\n' || c == C('D') || input.e == input.r + INPUT_BUF) {
					if (c == C('D'))
						consputc('\n');
					input.w = input.e;
					wakeup(&input.r);
				}
			}
			break;
		}
	}
	release(&cons.lock);
	if (doprocdump) {
		procdump(); // now call procdump() wo. cons.lock held
	}
}
__nonnull(1, 2) static int consoleread(struct inode *ip, char *dst, int n)
{
	uint32_t target;
	int c;

	iunlock(ip);
	target = n;
	acquire(&cons.lock);
	while (n > 0) {
		while (input.r == input.w) {
			if (myproc()->killed) {
				release(&cons.lock);
				ilock(ip);
				return -1;
			}
			sleep(&input.r, &cons.lock);
		}
		c = input.buf[input.r++ % INPUT_BUF];
		if (c == C('D')) { // EOF
			if (n < target) {
				// Save ^D for next time, to make sure
				// caller gets a 0-byte result.
				input.r--;
			}
			break;
		}
		*dst++ = c;
		--n;
		if (c == '\n')
			break;
	}
	release(&cons.lock);
	ilock(ip);

	return target - n;
}
/*
 * INVARIANT: None of the console_{height,width}_{text,pixels} functions should be
 * called before parse_multiboot(struct multiboot_info *).
 */
int
console_width_pixels(void)
{
	return __multiboot_console_width_pixels();
}

int
console_height_pixels(void)
{
	return __multiboot_console_height_pixels();
}

int
console_width_text(void)
{
	return __multiboot_console_width_text();
}

int
console_height_text(void)
{
	return __multiboot_console_height_text();
}

/*
 * This buffer is different from the `crt' buffer because it
 * collects characters and is only written to when an
 * fsync syscall or other flush syscalls happen.
 */
static char *console_buffer = NULL;
static int buffer_position = 0;
void
console_flush(void)
{
	for (int j = 0; j < buffer_position; j++) {
		consputc(console_buffer[j] & 0xff);
	}
	buffer_position = 0;
}
__nonnull(1, 2) static int consolewrite(struct inode *ip, char *buf, int n)
{
	iunlock(ip);
	acquire(&cons.lock);

	for (int i = 0; i < n; i++) {
		console_buffer[buffer_position] = buf[i];
		buffer_position++;
	}
	release(&cons.lock);
	ilock(ip);

	return n;
}

void
consoleinit(void)
{
	initlock(&cons.lock, "console");

	devsw[CONSOLE].write = consolewrite;
	devsw[CONSOLE].read = consoleread;
	cons.locking = 1;
	console_buffer = kmalloc(console_width_text() * console_height_text());

	ioapicenable(IRQ_KBD, 0);
}
