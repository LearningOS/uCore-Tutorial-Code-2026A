#include "timer.h"
#include "riscv.h"
#include "sbi.h"
#include "defs.h"

uint64 timer_frequency = 10000000;

static uint32 be32(const unsigned char *p)
{
	return ((uint32)p[0] << 24) | ((uint32)p[1] << 16) |
	       ((uint32)p[2] << 8) | p[3];
}

/* Firmware supplies the platform's counter rate in /cpus/timebase-frequency.
 * Parse before the allocator may reuse the firmware's DTB storage. */
void timer_configure(uint64 device_tree)
{
	const unsigned char *dtb = (const unsigned char *)device_tree;
	if (!dtb || be32(dtb) != 0xd00dfeed)
		panic("missing device tree");
	uint32 total = be32(dtb + 4), offset = be32(dtb + 8);
	uint32 strings_offset = be32(dtb + 12), strings_size = be32(dtb + 32);
	uint32 structure_size = be32(dtb + 36);
	if (offset > total || structure_size > total - offset ||
	    strings_offset > total || strings_size > total - strings_offset)
		panic("invalid device tree bounds");
	const unsigned char *p = dtb + offset, *end = p + structure_size;
	while (p + 4 <= end) {
		uint32 token = be32(p);
		p += 4;
		if (token == 1) {
			while (p < end && *p)
				p++;
			if (p == end)
				break;
			p = (const unsigned char *)(((uint64)p + 4) & ~3UL);
		} else if (token == 3) {
			if (p + 8 > end)
				break;
			uint32 size = be32(p), name = be32(p + 4);
			p += 8;
			if (size > (uint64)(end - p) || name >= strings_size)
				break;
			if (name + sizeof("timebase-frequency") <= strings_size &&
			    !memcmp(dtb + strings_offset + name, "timebase-frequency",
				    sizeof("timebase-frequency")) && size == 4) {
				timer_frequency = be32(p);
				if (!timer_frequency)
					panic("invalid timer frequency");
				return;
			}
			p += (size + 3) & ~3U;
		} else if (token == 9) {
			break;
		} else if (token != 2 && token != 4) {
			break;
		}
	}
	panic("missing timebase-frequency");
}

/// read the `mtime` regiser
uint64 get_cycle()
{
	return r_time();
}

/// Enable timer interrupt
void timer_init()
{
	// Enable supervisor timer interrupt
	w_sie(r_sie() | SIE_STIE);
	set_next_timer();
}

/// Set the next timer interrupt
void set_next_timer()
{
	const uint64 timebase = CPU_FREQ / TICKS_PER_SEC;
	set_timer(get_cycle() + timebase);
}
