#include "stat.h"
#include <errno.h>
#include <sys/stat.h>
#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <kernel/include/x86.h>
#include <string.h>
#include <unistd.h>
#include <stddef.h>
#include <time.h>

int errno;
extern char **environ;

// fill in st from pathname n
__attribute__((nonnull(2))) int
stat(const char *n, struct stat *st)
{
	int fd;
	int r;

	fd = open(n, O_RDONLY);
	if (fd < 0)
		return -1;
	r = fstat(fd, st);
	close(fd);
	return r;
}
// This should probably be moved into the kernel, because
// we have more symbolic link information there. It might
// be possible, however, to keep this in userspace if we
// can get enough information about files without too many
// syscalls.
int
lstat(const char *n, struct stat *st)
{
	return stat(n, st);
}

DIR *
fdopendir(int fd)
{
	struct dirent de;
	struct __linked_list_dirent *ll = malloc(sizeof(*ll));
	if (fd == -1)
		return NULL;
	DIR *dirp = (DIR *)malloc(sizeof(DIR));
	if (dirp == NULL) {
		close(fd);
		return NULL;
	}
	dirp->fd = fd;
	ll->prev = NULL;

	while (read(fd, &de, sizeof(de)) == sizeof(de) && strcmp(de.d_name, "") != 0) {
		ll->data = de;
		ll->next = malloc(sizeof(*ll));
		ll->next->prev = ll;
		ll = ll->next;
	}
	ll->next = NULL;
	ll->data = (struct dirent){0, ""};

	// "Rewind" dir
	while (ll->prev != NULL) {
		ll = ll->prev;
	}
	dirp->list = ll;
	return dirp;
}

struct dirent *
readdir(DIR *dirp)
{

	if (dirp == NULL || dirp->list == NULL) {
		errno = EBADF;
		return NULL;
	}
	struct dirent *to_ret = &dirp->list->data;
	dirp->list = dirp->list->next;
	if (strcmp(to_ret->d_name, "") == 0 && to_ret->d_ino == 0)
		return NULL;
	return to_ret;
}

DIR *
opendir(const char *path)
{
	int fd = open(path, O_RDONLY /*| O_DIRECTORY*/);
	if (fd == -1)
		return NULL;
	return fdopendir(fd);
}
int
closedir(DIR *dir)
{
	if (dir == NULL || dir->fd == -1) {
		return -1; // EBADF
	}
	int rc = close(dir->fd);
	if (rc == 0)
		dir->fd = -1;
	if (dir->list == NULL)
		return -1;
	while (dir->list != NULL && dir->list->next != NULL) {
		dir->list = dir->list->next;
	}
	dir->list = dir->list->prev;
	while (dir->list->prev != NULL) {
		free(dir->list->next);
	}
	free(dir->list);
	free(dir);
	return rc;
}

// no way to check for error...sigh....
// atoi("0") == 0
// atoi("V") == 0
// "V" != "0"
int
atoi(const char *s)
{
	int n = 0;

	while ('0' <= *s && *s <= '9')
		n = n * 10 + *s++ - '0';
	return n;
}
long long
strtoll(const char *restrict s, char **restrict endptr, int base)
{
	long long num = 0;
	bool positive = true;
	const char base_uppercase[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	const char base_lowercase[] = "0123456789abcdefghijklmnopqrstuvwxyz";
	if ((base != 0 && base < 2) || base > 36) {
		errno = EINVAL;
		return 0;
	}
	size_t i = 0;
	// skip whitespace
	while (isspace(s[i]))
		i++;
	if (s[i] == '+') {
		i++;
		positive = true;
	} else if (s[i] == '-') {
		i++;
		positive = false;
	}
	if (base == 0 || base == 16) {
		// Hexadecimal.
		if (strncmp(s + i, "0x", 2) == 0 || strncmp(s + i, "0X", 2) == 0) {
			base = 16;
			i += 2;
		// Octal.
		} else if (strncmp(s + i, "0", 1) == 0) {
			i++;
			base = 8;
		}
	}
	if (base <= 10) {
		while (s[i] != '\0' && '0' <= s[i] && s[i] <= '9')
			num = num * base + s[i++] - '0';
		return num;
	} else if (base <= 36) {
		while (s[i] != '\0' && (('0' <= s[i] && s[i] <= '9') ||
			('a' <= s[i] && s[i] <= 'z') || ('A' <= s[i] && s[i] <= 'Z'))) {
			if ('0' <= s[i] && s[i] <= '9')
				num = num * base + s[i++] - '0';
			if ('a' <= s[i] && s[i] <= 'z') {
				num = num * base + s[i++] - 'a';
			} else if ('A' <= s[i] && s[i] <= 'Z') {
				num = num * base + s[i++] - 'A';
			}
		}
		return num;
	}
	errno = EINVAL;
	return 0;

}

long
strtol(const char *restrict s, char **restrict nptr, int base)
{
	return (long)strtoll(s, nptr, base);
}

long
atol(const char *s)
{
	return strtol(s, NULL, 0);
}

long long
atoll(const char *s)
{
	return strtoll(s, NULL, 0);
}

int
atoi_base(const char *s, uint32_t base)
{
	return (int)strtoll(s, NULL, base);
}

void
assert_fail(const char *assertion, const char *file, int lineno,
						const char *func)
{
	fprintf(stderr, "%s:%d: %s: Assertion `%s' failed.\n", file, lineno, func,
					assertion);
	fprintf(stderr, "Aborting.\n");
	exit(-1);
}
int
isdigit(int c)
{
	return '0' <= c && c <= '9';
}

int
isspace(int c)
{
	return c == ' ' || c == '\f' || c == '\n' || c == '\r' || c == '\t' ||
				 c == '\v';
}
int
isalpha(int c)
{
	return ('a' <= c && c <= 'z') || ('A' <= c &&c <= 'Z');
}
int
isalnum(int c)
{
	return isalpha(c) || isdigit(c);
}

// Fun fact: setenv sets errno, but getenv does not :^)
char *
getenv(const char *name)
{
	for (size_t i = 0; environ[i] != NULL; i++) {
		char *equals = strchr(environ[i], '=');
		if (equals == NULL)
			continue; // Resilient. (Is this in the spec?)
		size_t this_env_length = equals - environ[i];
		// If it's not the same length, we don't even bother comparing.
		if (strlen(name) != this_env_length)
			continue;
		if (strncmp(name, environ[i], this_env_length) == 0) {
			return equals + 1;
		}
	}
	return NULL;
}

// Duplicate a string
// Caller frees the string.
char *
strdup(const char *s)
{
	if (s == NULL)
		return NULL;
	char *new_s = malloc(strlen(s) + 1);
	if (new_s == NULL) {
		errno = ENOMEM;
		return NULL;
	}
	strncpy(new_s, s, strlen(s) + 1);
	return new_s;
}

typedef void (*atexit_handler)(void);
static atexit_handler atexit_handlers[ATEXIT_MAX] = { NULL };

int
atexit(void (*function)(void))
{
	for (int i = 0; i < ATEXIT_MAX; i++) {
		if (atexit_handlers[i] == NULL) {
			atexit_handlers[i] = function;
			return 0;
		}
	}
	// IEEE Std 1003.1-2024 (POSIX.1-2024):
	// "Upon successful completion, atexit() shall return 0;"
	// "otherwise, it shall return a non-zero value."
	return -1;
}

__attribute__((noreturn)) void
exit(int status)
{
	for (int i = ATEXIT_MAX - 1; i >= 0; i--) {
		if (atexit_handlers[i] != NULL) {
			atexit_handlers[i]();
		}
	}
	_exit(status);
}

__attribute__((noreturn)) void
abort(void)
{
	_exit(1);
}

int
dup2(int oldfd, int newfd)
{
	return dup(oldfd);
}


extern int
__getcwd(char *buf, size_t n);
char *
getcwd(char *buf, size_t n)
{
	int ret = __getcwd(buf, n);
	if (ret < 0)
		return NULL;
	else
		return buf;
}

int
fcntl(int fd, int cmd, ...)
{
	fprintf(stderr, "fcntl: not implemented!\n");
	return -1;
}
int
isatty(int fd)
{
	return fd == 0;
}
