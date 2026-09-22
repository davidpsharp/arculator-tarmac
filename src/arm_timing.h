/*
  Arculator - Acorn Archimedes emulator

  ARM timing model: clock domains, DMA arbitration and the ARM3 cache.

  Split out of arm.c, which held two separate things: how long each memory
  access takes (this file) and what each instruction does (arm.c). Only the
  second is replaced when a different CPU core is used, so a core swap needs
  this half intact and calling the same way.

  Everything here runs on the emulator thread and works in `tsc`, the 32.32
  fixed-point cycle counter from timer.h, which the rest of the emulator
  (sound, video, IOC timers, podules) schedules against.
 */

#ifndef ARM_TIMING_H
#define ARM_TIMING_H

#include <stdint.h>

/*
 * How the next instruction fetch should be clocked.
 *
 * Set by the instruction being executed, consumed by the fetch at the top of
 * the run loop: an ARM2 merges the final writeback cycle with the next fetch,
 * an ARM3 does not.
 */
enum
{
	PROMOTE_NONE = 0,
	PROMOTE_MERGE,
	PROMOTE_NOMERGE
};

extern int promote_fetch_to_n;

/* S and N cycle lengths for the page the PC is in, updated by the fetch. */
extern int cyc_s, cyc_n;

/* An I-cycle is one FCLK tick: 1.0 in tsc's 32.32 fixed point. */
#define cyc_i (1ull << 32)

/* Video DMA fetch count, for the VIDC emulation's statistics. */
extern int vidc_fetches;

/*
 * Timing for one memory access.
 *
 * cache_read_timing() also fills the ARM3 cache when the address is cacheable
 * and missing, which is why it takes the merged-fetch flag as well.
 */
void cache_read_timing(uint32_t addr, int is_n_cycle, int is_merged_fetch);
void cache_write_timing(uint32_t addr, int is_n_cycle);

/* The last cycle of LDR/LDM/MUL/MLA and register-shift data processing. */
void merge_timing(uint32_t addr);

/* n internal cycles. */
void arm_clock_i(int i_cycles);

/* Invalidate the whole ARM3 cache (CP15 cache control writes this). */
void cache_flush(void);

/* Recalculate which DMA source is due next; call after changing a DMA timer. */
void recalc_min_timer(void);

/* Reset the clock domains, the DMA timers and the cache. */
void arm_timing_reset(void);

#endif /* ARM_TIMING_H */
