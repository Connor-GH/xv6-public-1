#pragma once
#include <stdint.h>
void
ioapicenable(int irq, int cpu);
extern uint8_t ioapicid;
void
ioapicinit(void);
