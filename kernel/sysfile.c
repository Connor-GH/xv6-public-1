//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include <defs.h>
#include <stdint.h>
#include <stat.h>
#include <stdlib.h>
#include <sys/uio.h>
#include <dirent.h>
#include <date.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <stddef.h>
#include "param.h"
#include "types.h"
#include "proc.h"
#include "fs.h"
#include "file.h"
#include "console.h"
#include "log.h"
#include "syscall.h"
#include "pipe.h"
#include "exec.h"
#include "kernel_string.h"
#include "drivers/lapic.h"
#include "vm.h"

static struct inode *
link_dereference(struct inode *ip, char *buff);
// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)
{
	int fd;
	struct file *f;

	if (argint(n, &fd) < 0)
		return -EINVAL;
	if (fd < 0 || fd >= NOFILE || (f = myproc()->ofile[fd]) == 0)
		return -EBADF;
	if (pfd)
		*pfd = fd;
	if (pf)
		*pf = f;
	return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
fdalloc(struct file *f)
{
	int fd;
	struct proc *curproc = myproc();

	for (fd = 0; fd < NOFILE; fd++) {
		if (curproc->ofile[fd] == 0) {
			curproc->ofile[fd] = f;
			return fd;
		}
	}
	return -1;
}

size_t
sys_dup(void)
{
	struct file *f;
	int fd;

	if (argfd(0, 0, &f) < 0)
		return -1;
	if ((fd = fdalloc(f)) < 0)
		return -EBADF;
	filedup(f);
	return fd;
}

size_t
sys_read(void)
{
	struct file *f;
	int n;
	char *p;

	// do not rearrange, because then 'n' will be undefined.
	if (argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argptr(1, &p, n) < 0)
		return -EINVAL;
	return fileread(f, p, n);
}

size_t
sys_write(void)
{
	struct file *f;
	int n;
	char *p;

	if (argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argptr(1, &p, n) < 0)
		return -EINVAL;
	return filewrite(f, p, n);
}

size_t
sys_writev(void)
{
	struct iovec *iovecs;
	int iovcnt;
	int fd;
	struct file *file;
	ssize_t accumulated_bytes = 0;
	if (argfd(0, &fd, &file) < 0 ||
			argptr(1, (void *)&iovecs, sizeof(*iovecs)) < 0 ||
			argint(2, &iovcnt) < 0) {
		return -EINVAL;
	}
	for (int i = 0; i < iovcnt; i++) {
		ssize_t ret = filewrite(file, iovecs->iov_base, iovecs->iov_len);
		if (ret < 0)
			return ret;
		accumulated_bytes += ret;
	}
	return accumulated_bytes;
}
size_t
sys_close(void)
{
	int fd;
	struct file *f;

	if (argfd(0, &fd, &f) < 0)
		return -EINVAL;
	myproc()->ofile[fd] = 0;
	fileclose(f);
	return 0;
}

size_t
sys_fstat(void)
{
	struct file *f;
	struct stat *st;

	if (argfd(0, 0, &f) < 0 || argptr(1, (void *)&st, sizeof(*st)) < 0)
		return -EINVAL;
	if (st == NULL)
		return -EINVAL;
	return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
size_t
sys_link(void)
{
	char name[DIRSIZ], *new, *old;
	int retflag = EINVAL;
	struct inode *dp, *ip;

	if (argstr(0, &old) < 0 || argstr(1, &new) < 0)
		return -EINVAL;

	begin_op();
	if ((ip = namei(old)) == 0) {
		end_op();
		return -ENOENT;
	}

	ilock(ip);
	if (S_ISDIR(ip->mode)) {
		iunlockput(ip);
		end_op();
		return -EISDIR;
	}

	ip->nlink++;
	iupdate(ip);
	iunlock(ip);

	if ((dp = nameiparent(new, name)) == 0) {
		retflag = ENOENT;
		goto bad;
	}
	ilock(dp);
	if (dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0) {
		iunlockput(dp);
		retflag = EXDEV; // probably incorrect
		goto bad;
	}
	iunlockput(dp);
	iput(ip);

	end_op();

	return 0;

bad:
	ilock(ip);
	ip->nlink--;
	iupdate(ip);
	iunlockput(ip);
	end_op();
	return -retflag;
}

// Is the directory dp empty except for "." and ".." ?
static int
isdirempty(struct inode *dp)
{
	int off;
	struct dirent de;

	for (off = 2 * sizeof(de); off < dp->size; off += sizeof(de)) {
		if (readi(dp, (char *)&de, off, sizeof(de)) != sizeof(de))
			panic("isdirempty: readi");
		if (de.inum != 0)
			return 0;
	}
	return 1;
}

size_t
sys_unlink(void)
{
	struct inode *ip, *dp;
	struct dirent de;
	char name[DIRSIZ], *path;
	uint32_t off;
	int error = EINVAL;

	if (argstr(0, &path) < 0)
		return -EINVAL;

	begin_op();
	if ((dp = nameiparent(path, name)) == 0) {
		end_op();
		return -ENOENT;
	}

	ilock(dp);

	// Cannot unlink "." or "..".
	if (namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
		goto bad;

	if ((ip = dirlookup(dp, name, &off)) == 0) {
		error = ENOENT;
		goto bad;
	}

	ilock(ip);

	if (ip->nlink < 1)
		panic("unlink: nlink < 1");
	if (S_ISDIR(ip->mode) && !isdirempty(ip)) {
		iunlockput(ip);
		error = ENOTEMPTY;
		goto bad;
	}

	memset(&de, 0, sizeof(de));
	if (writei(dp, (char *)&de, off, sizeof(de)) != sizeof(de))
		panic("unlink: writei");
	if (S_ISDIR(ip->mode)) {
		dp->nlink--;
		iupdate(dp);
	}
	iunlockput(dp);

	ip->nlink--;
	iupdate(ip);
	iunlockput(ip);

	end_op();

	return 0;

bad:
	iunlockput(dp);
	end_op();
	return -error;
}

// Holds lock on ip when released.
static struct inode *
create(char *path, mode_t mode, short major, short minor)
{
	struct inode *ip, *dp;
	char name[DIRSIZ];

	// get inode of path, and put the name in name.
	if ((dp = nameiparent(path, name)) == 0)
		return 0;
	ilock(dp);

	if ((ip = dirlookup(dp, name, 0)) != 0) {
		iunlockput(dp);
		ilock(ip);
		if (S_ISREG(ip->mode) && S_ISREG(mode)) {
			ip->mode = mode; // TODO limit permissions
			return ip;
		}
		if (S_ISLNK(ip->mode) && S_ISLNK(mode)) {
			ip->mode = mode;
			return ip;
		}
		iunlockput(ip);
		return 0;
	}

	if ((ip = ialloc(dp->dev, mode)) == 0)
		panic("create: ialloc");

	ilock(ip);
	ip->major = major;
	ip->minor = minor;
	ip->nlink = 1;
	ip->mode = mode;
	ip->gid = DEFAULT_GID;
	ip->uid = DEFAULT_UID;
	// atime, mtime, etc. get handled in iupdate()
	iupdate(ip);
	// Create . and .. entries.
	// because every directory goes as follows:
	// $ ls -l
	// .
	// ..
	// dir/
	// $ cd dir
	// $ ls -l
	// .
	// ..
	if (S_ISDIR(mode)) {
		dp->nlink++; // for ".."
		iupdate(dp);
		// No ip->nlink++ for ".": avoid cyclic ref count.
		if (dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
			panic("create dots");
	}

	if (dirlink(dp, name, ip->inum) < 0)
		panic("create: dirlink");

	iunlockput(dp);

	return ip;
}

int
fileopen(char *path, mode_t omode)
{
	int fd;
	struct file *f;
	struct inode *ip;

	if (path == NULL)
		return -EFAULT;

	begin_op();

	if (omode & O_CREATE) {
		// try to create a file and it exists.
		if ((ip = namei(path)) != 0) {
			// if it's a block device, possibly do something special.
			ilock(ip);
			if (S_ISBLK(ip->mode))
				goto get_fd;
			// if it's not a block device, just exit.
			iunlockput(ip);
			end_op();
			return -ENOTBLK;
		}
		// create() holds a lock on this inode pointer,
		// but only if it succeeds.
		ip = create(path, S_IFREG | S_IAUSR, 0, 0);
		if (ip == 0) {
			end_op();
			return -EIO;
		}
	} else {
		if ((ip = namei(path)) == 0) {
			end_op();
			return -ENOENT;
		}
		ilock(ip);

		if (S_ISLNK(ip->mode)) {
			if ((ip = link_dereference(ip, path)) == 0) {
				iunlockput(ip);
				end_op();
				return -EINVAL;
			}
		}
		if (S_ISDIR(ip->mode) && omode != O_RDONLY) {
			iunlockput(ip);
			end_op();
			return -EISDIR;
		}
	}
	// By this line, both branches above are holding a lock to ip.
	// That is why it is released down here.
get_fd:

	if ((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0) {
		if (f)
			fileclose(f);
		iunlockput(ip);
		end_op();
		return -EBADF;
	}
	iunlock(ip);
	end_op();

	f->type = FD_INODE;
	f->ip = ip;
	f->off = 0;
	f->readable = !(omode & O_WRONLY);
	f->writable = (omode & O_WRONLY) || (omode & O_RDWR);
	return fd;
}

size_t
sys_open(void)
{
	char *path;
	mode_t omode;

	if (argstr(0, &path) < 0 || argint(1, &omode) < 0)
		return -EINVAL;
	return fileopen(path, omode);
}

size_t
sys_mkdir(void)
{
	char *path;
	struct inode *ip;

	begin_op();
	if (argstr(0, &path) < 0 ||
			(ip = create(path, S_IFDIR | S_IAUSR, 0, 0)) == 0) {
		end_op();
		return -EINVAL;
	}
	iunlockput(ip);
	end_op();
	return 0;
}

size_t
sys_mknod(void)
{
	struct inode *ip;
	char *path;
	int major, minor;

	begin_op();
	if ((argstr(0, &path)) < 0 || argint(1, &major) < 0 ||
			argint(2, &minor) < 0 ||
			(ip = create(path, S_IFBLK | S_IAUSR, major, minor)) == 0) {
		end_op();
		return -EINVAL;
	}
	iunlockput(ip);
	end_op();
	return 0;
}

size_t
sys_chdir(void)
{
	char *path;
	struct inode *ip;
	struct proc *curproc = myproc();

	begin_op();
	if (argstr(0, &path) < 0 || (ip = namei(path)) == 0) {
		end_op();
		return -EINVAL;
	}
	ilock(ip);
	if (S_ISLNK(ip->mode)) {
		if ((ip = link_dereference(ip, path)) == 0) {
			end_op();
			panic("open link_dereference");
		}
	}
	if (!S_ISDIR(ip->mode)) {
		iunlockput(ip);
		end_op();
		return -ENOTDIR;
	}
	iunlock(ip);
	iput(curproc->cwd);
	end_op();
	curproc->cwd = ip;
	return 0;
}

size_t
sys_execve(void)
{
	char *path, *argv[MAXARG], *envp[MAXENV];
	uintptr_t uargv, uarg;
	uintptr_t uenvp, uenv;

	if (argstr(0, &path) < 0 || arguintptr_t(1, &uargv) < 0 ||
			arguintptr_t(2, &uenvp) < 0) {
		return -EINVAL;
	}
	memset(argv, 0, sizeof(argv));
	memset(envp, 0, sizeof(envp));
	for (int i = 0;; i++) {
		if (i >= NELEM(argv))
			return -ENOEXEC;
		if (fetchuintptr_t(uargv + sizeof(uintptr_t) * i, &uarg) < 0)
			return -ENOEXEC;
		if (uarg == 0) {
			argv[i] = 0;
			break;
		}
		if (fetchstr(uarg, &argv[i]) < 0)
			return -EINVAL;
	}
	for (int i = 0;; i++) {
		if (i >= NELEM(envp))
			return -ENOEXEC;
		if (fetchuintptr_t(uenvp + sizeof(uintptr_t) * i, &uenv) < 0)
			return -ENOEXEC;
		if (uenv == 0) {
			envp[i] = 0;
			break;
		}
		if (fetchstr(uenv, &envp[i]) < 0)
			return -EINVAL;
	}
	return execve(path, argv, envp);
}

size_t
sys_pipe(void)
{
	int *fd;
	struct file *rf, *wf;
	int fd0, fd1;

	if (argptr(0, (void *)&fd, 2 * sizeof(fd[0])) < 0)
		return -EINVAL;
	if (pipealloc(&rf, &wf) < 0)
		return -EINVAL;
	fd0 = -1;
	if ((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0) {
		if (fd0 >= 0)
			myproc()->ofile[fd0] = 0;
		fileclose(rf);
		fileclose(wf);
		return -EBADF;
	}
	fd[0] = fd0;
	fd[1] = fd1;
	return 0;
}

size_t
sys_chmod(void)
{
	char *path;
	mode_t mode;
	struct inode *ip;
	begin_op();
	if (argstr(0, &path) < 0 || argint(1, (mode_t *)&mode) < 0 ||
			(ip = namei(path)) == 0) {
		end_op();
		return -EINVAL;
	}
	ilock(ip);
	// capture the file type and change the permissions
	ip->mode = (ip->mode & S_IFMT) | mode;
	iunlock(ip);
	end_op();
	return 0;
}

size_t
sys_echoout(void)
{
	int answer;
	begin_op();
	if (argint(0, &answer) < 0) {
		end_op();
		return -EINVAL;
	}
	echo_out = answer;
	end_op();
	return 0;
}

// target, linkpath
size_t
sys_symlink(void)
{
	char *target, *linkpath;
	char dir[DIRSIZ];
	uint32_t poff;
	struct inode *eexist, *ip;
	if (argstr(0, &target) < 0 || argstr(1, &linkpath) < 0)
		return -EINVAL;

	begin_op();
	if ((eexist = namei(linkpath)) != 0) {
		end_op();
		return -EEXIST;
	}
	if ((eexist = nameiparent(linkpath, dir)) == 0) {
		end_op();
		return -EEXIST;
	}

	// Dirlookup's first arg needs a lock.
	ilock(eexist);

	if ((ip = dirlookup(eexist, dir, &poff)) != 0) {
		iunlockput(eexist);
		end_op();
		return -EEXIST;
	}
	iunlock(eexist);

	if ((ip = create(linkpath, S_IFLNK | S_IAUSR, 0, 0)) == 0) {
		end_op();
		return -ENOSPC;
	}
	if (writei(ip, target, 0, strlen(target) + 1) != strlen(target) + 1)
		panic("symlink writei");

	iunlockput(ip);
	end_op();

	return 0;
}

size_t
sys_readlink(void)
{
	char *target, *ubuf;
	int bufsize = 0;
	if (argstr(0, &target) < 0 || argstr(1, &ubuf) < 0 ||
			argint(2, &bufsize) < 0) {
		return -EINVAL;
	}
	struct inode *ip;
	begin_op();
	if ((ip = namei(target)) == 0) {
		return -ENOENT;
	}

	ilock(ip);

	if (!S_ISLNK(ip->mode)) {
		iunlock(ip);
		end_op();
		return -EINVAL;
	}

	if (ip->size > bufsize) {
		iunlock(ip);
		end_op();
		return -EINVAL;
	}

	if (readi(ip, ubuf, 0, bufsize) < 0)
		panic("readlink readi");

	if (copyout(myproc()->pgdir, (uintptr_t)ubuf, ubuf, bufsize) < 0)
		panic("readlink copyout");

	iunlock(ip);
	end_op();
	return 0;
}

struct inode *
link_dereference(struct inode *ip, char *buff)
{
	int ref_count = NLINK_DEREF;
	struct inode *new_ip = ip;
	while (S_ISLNK(new_ip->mode)) {
		ref_count--;
		if (ref_count == 0)
			goto bad;

		if (readi(new_ip, buff, 0, new_ip->size) < 0)
			goto bad;

		iunlock(new_ip);
		if ((new_ip = namei(buff)) == 0)
			return 0;

		ilock(new_ip);
	}
	return new_ip;

bad:
	iunlock(new_ip);
	return 0;
}

size_t
sys_lseek(void)
{
	int fd;
	off_t offset;
	int whence;
	struct file *file;

	if (argfd(0, &fd, &file) < 0 || argssize_t(1, &offset) < 0 ||
			argint(2, &whence) < 0)
		return -EINVAL;
	if (S_ISFIFO(file->ip->mode) || S_ISSOCK(file->ip->mode))
		return -ESPIPE;

	return fileseek(file, offset, whence);
}

/* Unimplemented */
size_t
sys_fsync(void)
{
	int fd;
	struct file *file;
	if (argfd(0, &fd, &file) < 0)
		return -EINVAL;

	if (fd == 0 || fd == 1 || fd == 2) {
		if (fd == 0)
			return -EINVAL;
		return 0;
	}
	return 0;
}
