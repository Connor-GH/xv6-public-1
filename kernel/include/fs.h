#pragma once
// On-disk file system format.
// Both the kernel and user programs use this header file.
#ifndef USE_HOST_TOOLS
#include <stdint.h>
#include "sleeplock.h"
#else
#include <../../include/stdint.h>
#include <../../kernel/include/sleeplock.h>
#endif

// Disk layout:
// [ boot block | super block | log | inode blocks |
//                                          free bit map | data blocks]
//
// mkfs computes the super block and builds an initial file system. The
// super block describes the disk layout:
struct superblock {
	uint32_t size; // Size of file system image (blocks)
	uint32_t nblocks; // Number of data blocks
	uint32_t ninodes; // Number of inodes.
	uint32_t nlog; // Number of log blocks
	uint32_t logstart; // Block number of first log block
	uint32_t inodestart; // Block number of first inode block
	uint32_t bmapstart; // Block number of first free map block
};
#define NDIRECT 6U
#define NINDIRECT (BSIZE / sizeof(uint32_t))
#define MAXFILE (NDIRECT + NINDIRECT + (NINDIRECT * NINDIRECT))
#define NDINDIRECT_PER_ENTRY NDIRECT
#define NDINDIRECT_ENTRY NDIRECT
// in-memory copy of an inode
struct inode {
	uint32_t dev; // Device number
	uint32_t inum; // Inode number
	int ref; // Reference count
	struct sleeplock lock; // protects everything below here
	int valid; // inode has been read from disk?

	short major;
	short minor;
	short nlink;
	uint32_t size;
	uint32_t mode; // copy of disk inode
	uint16_t gid;
	uint16_t uid;
	uint32_t ctime; // change
	uint32_t atime; // access
	uint32_t mtime; // modification
	uint32_t addrs[NDIRECT + 1 + 1];
};
// On-disk inode structure
struct dinode {
	short major; // Major device number (T_DEV only)
	short minor; // Minor device number (T_DEV only)
	short nlink; // Number of links to inode in file system
	uint32_t size; // Size of file (bytes)
	uint32_t mode; // File type and permissions
	uint16_t gid;
	uint16_t uid;
	uint32_t ctime; // change
	uint32_t atime; // access
	uint32_t mtime; // modification
	uint32_t addrs[NDIRECT + 1 + 1]; // Data block addresses
};

// Inodes per block.
#define IPB (BSIZE / sizeof(struct dinode))

// Block containing inode i
#define IBLOCK(i, sb) ((i) / IPB + sb.inodestart)

// Bitmap bits per block
#define BPB (BSIZE * 8)

// Block of free map containing bit for block b
#define BBLOCK(b, sb) (b / BPB + sb.bmapstart)

// Directory is a file containing a sequence of dirent structures.
#define DIRSIZ 254U
#define ROOTINO 1U // root i-number
#define BSIZE 2048U // block size

#ifndef USE_HOST_TOOLS
#include "stat.h"
void
readsb(int dev, struct superblock *sb);
int
dirlink(struct inode *, const char *, uint32_t);
struct inode *
dirlookup(struct inode *, const char *, uint32_t *);
struct inode *ialloc(uint32_t, int32_t);
struct inode *
idup(struct inode *);
void
iinit(int dev);
void
ilock(struct inode *);
void
iput(struct inode *);
void
iunlock(struct inode *);
void
iunlockput(struct inode *);
void
iupdate(struct inode *);
int
namecmp(const char *, const char *);
struct inode *
namei(char *);
struct inode *
nameiparent(char *, char *);
int
readi(struct inode *, char *, uint32_t, uint32_t);
void
stati(struct inode *, struct stat *);
int
writei(struct inode *, char *, uint32_t, uint32_t);
#endif
