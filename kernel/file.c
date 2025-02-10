//
// File descriptors
//

#include "fs.h"
#include <errno.h>
#include "param.h"
#include "spinlock.h"
#include "file.h"
#include "console.h"
#include "pipe.h"
#include "log.h"
#include "proc.h"
#include "lseek.h"

struct devsw devsw[NDEV];
struct {
	struct spinlock lock;
	struct file file[NFILE];
} ftable;

struct file *
fd_to_struct_file(int fd)
{
	return myproc()->ofile[fd];
}
void
fileinit(void)
{
	initlock(&ftable.lock, "ftable");
}

// Allocate a file structure.
struct file *
filealloc(void)
{
	struct file *f;

	acquire(&ftable.lock);
	for (f = ftable.file; f < ftable.file + NFILE; f++) {
		if (f->ref == 0) {
			f->ref = 1;
			release(&ftable.lock);
			return f;
		}
	}
	release(&ftable.lock);
	return 0;
}

// Increment ref count for file f.
struct file *
filedup(struct file *f)
{
	acquire(&ftable.lock);
	if (unlikely(f->ref < 1))
		panic("filedup");
	f->ref++;
	release(&ftable.lock);
	return f;
}

// Close file f.  (Decrement ref count, close when reaches 0.)
void
fileclose(struct file *f)
{
	struct file ff;

	acquire(&ftable.lock);
	if (unlikely(f->ref < 1))
		panic("fileclose");
	if (--f->ref > 0) {
		release(&ftable.lock);
		return;
	}
	ff = *f;
	f->ref = 0;
	f->type = FD_NONE;
	release(&ftable.lock);

	if (ff.type == FD_PIPE)
		pipeclose(ff.pipe, ff.writable);
	else if (ff.type == FD_INODE) {
		begin_op();
		inode_put(ff.ip);
		end_op();
	}
}

// Get metadata about file f.
int
filestat(struct file *f, struct stat *st)
{
	if (f->type == FD_INODE) {
		inode_lock(f->ip);
		inode_stat(f->ip, st);
		inode_unlock(f->ip);
		return 0;
	}
	return -ENOENT;
}

// Read from file f.
int
fileread(struct file *f, char *addr, int n)
{
	int r;

	if (f->readable == 0)
		return -EINVAL;
	if (f->type == FD_PIPE)
		return piperead(f->pipe, addr, n);
	if (f->type == FD_INODE) {
		inode_lock(f->ip);
		if ((r = inode_read(f->ip, addr, f->off, n)) > 0)
			f->off += r;
		inode_unlock(f->ip);
		return r;
	}
	panic("fileread");
}

int
fileseek(struct file *f, int n, int whence)
{
	int offset = 0;
	if (whence == SEEK_CUR) {
		offset = f->off + n;
	} else if (whence == SEEK_SET) {
		offset = n;
	} else if (whence == SEEK_END) {
		// Not sure if this is right.
		offset = f->ip->size;
	} else {
		return -EINVAL;
	}
	f->off = offset;
	return 0;
}

// Write to file f.
int
filewrite(struct file *f, char *addr, int n)
{
	int r;

	if (f->writable == 0)
		return -EROFS;
	if (f->type == FD_PIPE)
		return pipewrite(f->pipe, addr, n);
	if (f->type == FD_INODE) {
		// write a few blocks at a time to avoid exceeding
		// the maximum log transaction size, including
		// i-node, indirect block, allocation blocks,
		// and 2 blocks of slop for non-aligned writes.
		// this really belongs lower down, since inode_write()
		// might be writing a device like the console.
		int max = ((MAXOPBLOCKS - 1 - 1 - 2) / 2) * 512;
		int i = 0;
		while (i < n) {
			int n1 = n - i;
			if (n1 > max)
				n1 = max;

			begin_op();
			inode_lock(f->ip);
			if ((r = inode_write(f->ip, addr + i, f->off, n1)) > 0)
				f->off += r;
			inode_unlock(f->ip);
			end_op();

			if (r < 0)
				break;
			if (r != n1)
				panic("short filewrite");
			i += r;
		}
		return i == n ? n : -EDOM;
	}
	panic("filewrite");
}
