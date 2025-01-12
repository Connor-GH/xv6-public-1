#pragma once

#include "stdint.h"
#include <stddef.h>
#include <sys/types.h>
int
fork(void) __attribute__((returns_twice));
void
exit(int) __attribute__((noreturn));
int
pipe(int *);
int
execve(char *, char **, char **);
static inline int
exec(char *prog, char **argv)
{
	return execve(prog, argv, (char *[]){ "", NULL });
}
// TODO our exec has hardcoded PATH
// convert into environment variable.
static inline int
execvp(char *file, char **argv)
{
	return exec(file, argv);
}
// our exec() is technically execv()
static inline int
execv(char *prog, char **argv)
{
	return exec(prog, argv);
}
int
write(int, const void *, int);
int
read(int, void *, int);
int
close(int);
int
unlink(const char *);
int
link(const char *, const char *);
int
symlink(const char *target, const char *linkpath);
int
readlink(const char *restrict pathname, char *restrict linkpath, size_t buf);
int
chdir(const char *);
int
dup(int);
int
getpid(void);
void *
sbrk(int);
int
sleep(int);
// needs sys/reboot
int
reboot(int cmd);
int
setuid(int);
int
fsync(int fd);
extern char *optarg;
extern int optind, opterr, optopt;
int
getopt(int argc, char *const argv[], const char *optstring);

enum {
	SEEK_SET,
	SEEK_CUR,
	SEEK_END,
};

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
off_t
lseek(int fd, off_t offset, int whence);
