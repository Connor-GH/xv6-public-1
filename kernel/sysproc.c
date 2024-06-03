#include "../include/types.h"
#include "../include/defs.h"
#include "../include/date.h"
#include "include/x86.h"
#include "drivers/memlayout.h"
#include "include/proc.h"

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0; // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if (argint(0, &pid) < 0)
	return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if (argint(0, &n) < 0)
	return -1;
  addr = myproc()->sz;
  if (growproc(n) < 0)
	return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if (argint(0, &n) < 0)
	return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while (ticks - ticks0 < n) {
	if (myproc()->killed) {
	  release(&tickslock);
	  return -1;
	}
	sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

int sys_date(void) {
  struct rtcdate *r = (struct rtcdate *)(myproc()->tf->esp+4+4*6);
  cmostime(r);
  r->hour -= 5;
  return 0;
}
