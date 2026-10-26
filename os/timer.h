#ifndef TIMER_H
#define TIMER_H

#include "types.h"

#define TICKS_PER_SEC (100)
// QEMU
extern uint64 timer_frequency;
#define CPU_FREQ timer_frequency

uint64 get_cycle();
void timer_init();
void timer_configure(uint64 device_tree);
void set_next_timer();

typedef struct {
	uint64 sec; // 自 Unix 纪元起的秒数
	uint64 usec; // 微秒数
} TimeVal;

#endif // TIMER_H
