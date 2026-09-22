/*Arculator 2.2 by Sarah Walker
  ARM2 & ARM3 timing: clock domains, DMA arbitration and the ARM3 cache

  Split out of arm.c so that the interpreter can be replaced without
  disturbing the timing model; the code itself is unchanged. See
  arm_timing.h for what the rest of the emulator uses from here.*/
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "arc.h"
#include "arm.h"
#include "arm_timing.h"
#include "cp15.h"
#include "ioc.h"
#include "mem.h"
#include "memc.h"
#include "timer.h"
#include "vidc.h"

/*Shared with the interpreter: set by the instruction fetch, read by the
  clock functions below.*/
int cyc_s, cyc_n;
int promote_fetch_to_n = PROMOTE_NONE;
int vidc_fetches = 0;
static uint8_t arm3_cache[((1 << 26) >> 4) >> 3];
static uint32_t arm3_cache_tag[4][64];
static int arm3_slot = 1;
#define TAG_INVALID -1



/*Archimedes has three clock domains :
	FCLK - fast CPU clock
	MCLK - MEMC clock
	IOCLK - IOC clock

  IOCLK is always 8 MHz. MCLK is either 8 or 12 on unmodified machines.

  Synchronising to MCLK costs 1F + 2L cycles - presumably 1F for cache lookup,
  2L for sync.

  During a cache fetch, ARM3 will be clocked once the request word has been
  fetched. It will continue to be clocked for successive S or I cycles.
*/
enum
{
	DOMAIN_FCLK,
	DOMAIN_MCLK,
	DOMAIN_IOCLK
};
static int clock_domain;

/*Timestamp of when memory is next available*/
static uint64_t mem_available_ts;

static uint64_t refresh_ts;

/*Remaining read cycles in the current line fill. If non-zero then I and S cycles
  will be promoted to MCLK-based S cycles. N cycles will have to wait until
  mem_available_ts*/
static int pending_reads = 0;
static uint32_t cache_fill_addr;

/*DMA priorities :
	Video/Cursor
	Sound
	Refresh*/
/*Two cases :
	On N-cycle - run until no pending DMA requests. TSC will need to be updated and
		timers need to run
	On I-cycle - run until TSC. TSC should not be updated, timers do not need to run*/

enum
{
	DMA_REFRESH,
	DMA_SOUND,
	DMA_CURSOR,
	DMA_VIDEO
};
static uint64_t min_timer;
static int next_dma_source;
void recalc_min_timer(void)
{
	min_timer = refresh_ts;
	next_dma_source = DMA_REFRESH;

	if (memc_dma_sound_req && TIMER_VAL_LESS_THAN_VAL_64(memc_dma_sound_req_ts, min_timer))
	{
		min_timer = memc_dma_sound_req_ts;
		next_dma_source = DMA_SOUND;
	}
	if (memc_videodma_enable && memc_dma_cursor_req && TIMER_VAL_LESS_THAN_VAL_64(memc_dma_cursor_req_ts, min_timer))
	{
		min_timer = memc_dma_cursor_req_ts;
		next_dma_source = DMA_CURSOR;
	}
	if (memc_videodma_enable && memc_dma_video_req && TIMER_VAL_LESS_THAN_VAL_64(memc_dma_video_req_ts, min_timer))
	{
		min_timer = memc_dma_video_req_ts;
		next_dma_source = DMA_VIDEO;
	}
}

static void run_dma(int update_tsc)
{
	while (TIMER_VAL_LESS_THAN_VAL_64(min_timer, tsc))
	{
		switch (next_dma_source)
		{
			case DMA_REFRESH:
//                        if (output) rpclog("Refresh DMA %i\n", mem_dorefresh);
			if (mem_dorefresh)
			{
				if (TIMER_VAL_LESS_THAN_VAL_64(refresh_ts, mem_available_ts))
					mem_available_ts += mem_spd_multi_2;
				else
					mem_available_ts = refresh_ts + mem_spd_multi_2;
			}
			refresh_ts += mem_spd_multi_32;
			break;
			case DMA_SOUND:
//                        if (output) rpclog("Sound DMA\n");
			if (TIMER_VAL_LESS_THAN_VAL_64(memc_dma_sound_req_ts, mem_available_ts))
				mem_available_ts += mem_spd_multi_5;
			else
				mem_available_ts = memc_dma_sound_req_ts + mem_spd_multi_5;
			memc_dma_sound_req = 0;
			break;
			case DMA_CURSOR:
//                        if (output) rpclog("Cursor DMA\n");
			if (TIMER_VAL_LESS_THAN_VAL_64(memc_dma_cursor_req_ts, mem_available_ts))
				mem_available_ts += mem_spd_multi_5;
			else
				mem_available_ts = memc_dma_cursor_req_ts + mem_spd_multi_5;
			memc_dma_cursor_req = 0;
			break;
			case DMA_VIDEO:
//                        if (output) rpclog("Video fetch %i\n", memc_dma_video_req);
			if (TIMER_VAL_LESS_THAN_VAL_64(memc_dma_video_req_ts, mem_available_ts))
				mem_available_ts += memc_dma_video_req * mem_spd_multi_5;
			else
				mem_available_ts = memc_dma_video_req_ts + memc_dma_video_req * mem_spd_multi_5;
			if (memc_dma_video_req == 2)
			{
				memc_dma_video_req_ts = memc_dma_video_req_start_ts;
				memc_dma_video_req = 1;
			}
			else
				memc_dma_video_req_ts += memc_dma_video_req_period;
			break;
		}
		if (update_tsc && TIMER_VAL_LESS_THAN_VAL_64(tsc, mem_available_ts))
		{
			tsc = mem_available_ts;
			if (TIMER_VAL_LESS_THAN_VAL(timer_target, tsc >> 32))
				timer_process();
		}
		recalc_min_timer();
	}
}

static void sync_to_mclk(void)
{
	if (clock_domain != DOMAIN_MCLK)
	{
		uint64_t sync_cycles = mem_spd_multi - (tsc % mem_spd_multi);
		tsc += sync_cycles; /*Now synchronised to MCLK*/
		tsc += mem_spd_multi; /*Synchronising takes another L cycle according to the ARM3 datasheet*/
	}

	if (TIMER_VAL_LESS_THAN_VAL(timer_target, tsc >> 32))
		timer_process();

	clock_domain = DOMAIN_MCLK;
	run_dma(1);
}
static void sync_to_fclk(void)
{
	if (clock_domain != DOMAIN_FCLK)
	{
		tsc = (tsc + 0xffffffffull) & ~0xffffffffull;

		clock_domain = DOMAIN_FCLK;
	}
}

static uint64_t last_cycle_length = 0;

static void CLOCK_N(uint32_t addr)
{
	tsc += mem_speed[(addr >> 12) & 0x3fff][1];
	last_cycle_length = mem_speed[(addr >> 12) & 0x3fff][1];
}

static void CLOCK_S(uint32_t addr)
{
	tsc += mem_speed[(addr >> 12) & 0x3fff][0];
	last_cycle_length = mem_speed[(addr >> 12) & 0x3fff][0];
}

static void CLOCK_I()
{
	if (pending_reads)
	{
		/*Cache fill is in progress. As CPU is currently synced to MCLK,
		  'promote' this cycle to an S-cycle*/
		pending_reads--;
		CLOCK_S(cache_fill_addr);
	}
	else
	{
//                if (output)
//                        rpclog(" CLOCK_I run_dma\n");
		run_dma(0);
		if (memc_is_memc1)
			tsc += last_cycle_length;
		else
			tsc += cyc_i;
	}
}

void arm_clock_i(int i_cycles)
{
	while (i_cycles--)
		CLOCK_I();
}

static void cache_line_fill(uint32_t addr)
{
	int byte_offset = addr >> (4+3);
	int bit_offset = (addr >> 4) & 7;
	int set = (addr >> 4) & 3;

#ifndef RELEASE_BUILD
	if (addr & ~0x3ffffff)
		fatal("cache_line_fill outside of valid range %08x\n", addr);
#endif

	if (arm3_cache_tag[set][arm3_slot] != TAG_INVALID)
	{
		int old_bit_offset = (arm3_cache_tag[set][arm3_slot] >> 4) & 7;
		int old_byte_offset = (arm3_cache_tag[set][arm3_slot] >> (4+3));

		arm3_cache[old_byte_offset] &= ~(1 << old_bit_offset);
	}

	arm3_cache_tag[set][arm3_slot] = addr & ~0xf;
	arm3_cache[byte_offset] |= (1 << bit_offset);
	sync_to_mclk();

	mem_available_ts = tsc + mem_speed[addr >> 12][1] + 3*mem_speed[addr >> 12][0];

	/*ARM3 will start to clock the CPU again once the requested word has been
	  read. So only 'charge' the emulated CPU up to that point, and promote
	  subsequent cycles to memory cycles until the line fill has completed*/
	CLOCK_N(addr);
	if ((addr & 0xc) >= 4)
	{
		CLOCK_S(addr);
		if ((addr & 0xc) >= 8)
		{
			CLOCK_S(addr);
			if ((addr & 0xc) >= 0xc)
				CLOCK_S(addr);
		}
	}
	pending_reads = 3 - ((addr & 0xc) >> 2);
	cache_fill_addr = addr & ~0xf;

	if (!((arm3_slot ^ (arm3_slot >> 1)) & 1))
		arm3_slot |= 0x40;
	arm3_slot >>= 1;

//        rpclog("Cache line fill %08x. %i pending reads\n", addr, pending_reads);
}

static int cache_was_on = 0;
void cache_read_timing(uint32_t addr, int is_n_cycle, int is_merged_fetch)
{
#ifndef RELEASE_BUILD
	if (addr & ~0x3ffffff)
		fatal("cache_read_timing outside of valid range %08x\n", addr);
#endif

  //      if (output) rpclog("Read %c-cycle %07x\n", is_n_cycle?'N':'S', addr);
	if (is_n_cycle)
	{
		cache_was_on = 0;
		if (cp15_cacheon)
		{
//                        rpclog("N-cycle %08x %i %i\n", addr, clock_domain, pending_reads);
			if (pending_reads)
			{
#ifndef RELEASE_BUILD
				if (clock_domain != DOMAIN_MCLK)
					fatal("N-cycle with pending reads - clock_domain != MCLK %07x\n", addr);
				if (TIMER_VAL_LESS_THAN_VAL_64(mem_available_ts, tsc))
					fatal("N-cycle with pending reads - TS already expired? %016llx %016llx\n", mem_available_ts, tsc);
#endif
				/*Complete pending line fill*/
				pending_reads = 0;
				tsc = mem_available_ts;
			}

			/*Always start N-cycle synced to FCLK - this is required for
			  cache lookup*/
			sync_to_fclk();

			int bit_offset = (addr >> 4) & 7;
			int byte_offset = addr >> (4+3);

			if (arm3_cache[byte_offset] & (1 << bit_offset))
			{
				cache_was_on = 1;
				CLOCK_I(); /*Data is in cache*/
			}
			else if (!(arm3cp.cache & (1 << (addr >> 21))))
			{
				/*Data is uncacheable*/
				sync_to_mclk();
				CLOCK_N(addr);
			}
			else
			{
				/*Data not in cache; perform cache fill*/
				cache_line_fill(addr);
			}
		}
		else
		{
			sync_to_mclk();
			CLOCK_N(addr);
			mem_available_ts = tsc;
			/*Merged fetch doesn't cause extended I-cycle on MEMC1*/
			if (memc_is_memc1 && is_merged_fetch == PROMOTE_MERGE)
				last_cycle_length = mem_speed[(addr >> 12) & 0x3fff][0];
		}
	}
	else
	{
		if (cp15_cacheon)
		{
//                        rpclog("S-cycle %08x %i %i\n", addr, clock_domain, pending_reads);
			if (pending_reads)
			{
#ifndef RELEASE_BUILD
				if (clock_domain != DOMAIN_MCLK)
					fatal("S-cycle with pending reads - clock_domain != MCLK %07x\n", addr);
				if (TIMER_VAL_LESS_THAN_VAL_64(mem_available_ts, tsc))
					fatal("S-cycle with pending reads - TS already expired? %016llx %016llx\n", mem_available_ts, tsc);
#endif
				/*Cache line fill is in progress. Since the CPU
				  being clocked and this is an S cycle, it must
				  be the next word to be read*/
				pending_reads--;
#ifndef RELEASE_BUILD
				if (clock_domain != DOMAIN_MCLK)
					fatal("Data uncacheable - clock_domain != MCLK %07x\n", addr);
#endif
				CLOCK_S(addr);
			}
			else if (cache_was_on)
			{
#ifndef RELEASE_BUILD
				int bit_offset = (addr >> 4) & 7;
				int byte_offset = ((addr & 0x3ffffff) >> (4+3));

				if (!(arm3_cache[byte_offset] & (1 << bit_offset)))
					fatal("S-cycle - cache_was_on but data not in cache %08x\n", addr);
				if ((clock_domain != DOMAIN_FCLK) && ((addr & ~0xf) != cache_fill_addr))
					fatal("Data in cache - clock_domain != FCLK %08x\n", addr);
#endif
				sync_to_fclk();
				CLOCK_I(); /*Data is in cache*/
			}
			else if (!(arm3cp.cache & (1 << (addr >> 21))))
			{
				/*Data is uncacheable*/
#ifndef RELEASE_BUILD
				if (clock_domain != DOMAIN_MCLK)
					fatal("Data uncacheable - clock_domain != MCLK %07x\n", addr);
#endif
				CLOCK_S(addr);
			}
			else
			{
				cache_line_fill(addr);
//                                sync_to_fclk();
//                                fatal("Data not in cache for S cycle - should not be currently possible  %08x\n", addr);
			}
		}
		else
		{
			CLOCK_S(addr);
			mem_available_ts = tsc;
		}
	}
}

void cache_flush()
{
	int set, slot;
//	rpclog("cache_flush\n");
	for (set = 0; set < 4; set++)
	{
		for (slot = 0; slot < 64; slot++)
		{
			if (arm3_cache_tag[set][slot] != TAG_INVALID)
			{
				int bit_offset = (arm3_cache_tag[set][slot] >> 4) & 7;
				int byte_offset = (arm3_cache_tag[set][slot] >> (4+3));

				arm3_cache[byte_offset] &= ~(1 << bit_offset);

				arm3_cache_tag[set][arm3_slot] = TAG_INVALID;
			}
		}
	}

	cache_was_on = 0;

/*	for (set = 0; set < (((1 << 26) >> 4) >> 3); set++)
	{
		if (arm3_cache[set])
			fatal("Flush didn't flush - %x %x\n", set, arm3_cache[set]);
	}*/
}

void cache_write_timing(uint32_t addr, int is_n_cycle)
{
	addr &= 0x3ffffff;

//        if (output) rpclog("Write %c-cycle %08x\n", is_n_cycle ? 'N' : 'S', addr);
	if (pending_reads)
	{
#ifndef RELEASE_BUILD
		if (clock_domain != DOMAIN_MCLK)
			fatal("Write cycle with pending reads - clock_domain != MCLK %07x\n", addr);
		if (TIMER_VAL_LESS_THAN_NE_VAL_64(mem_available_ts, tsc))
			fatal("Write cycle with pending reads - TS already expired? %016llx %016llx\n", mem_available_ts, tsc);
#endif
		/*Complete pending line fill*/
		/*Note that ARM3 can go straight from a line fill to memory write
		  without resyncing to FCLK if the line fill is still incomplete
		  when the write is requested*/
		pending_reads = 0;
		tsc = mem_available_ts;
	}

	if (arm3cp.disrupt & (1 << (addr >> 21)))
		cache_flush();

	if (is_n_cycle)
	{
//                rpclog("Write N-cycle %08x\n", addr);
		sync_to_mclk();
		CLOCK_N(addr);
		mem_available_ts = tsc;
	}
	else
	{
//                rpclog("Write S-cycle %08x\n", addr);
#ifndef RELEASE_BUILD
		if (clock_domain != DOMAIN_MCLK)
			fatal("Write S-cycle - not in MCLK %08x\n", addr);
#endif
		CLOCK_S(addr);
		mem_available_ts = tsc;
	}
}


/*Handle timing for last cycle of LDR/LDM/MUL/MLA/data processing intrucstions
  with register shift. On ARM2 machines this will be 'merged' with the following
  instruction fetch. On ARM3 the final cycle is just an I-cycle.*/
void merge_timing(uint32_t addr)
{
	promote_fetch_to_n = PROMOTE_MERGE; /*Merge writeback with next fetch*/
	if (memc_is_memc1)
	{
		if ((addr & 0xc) == 0xc)
		{
			/*MEMC1 does _not_ merge if A[2:3]=11, so clock an extra
			  I-cycle and push the next fetch to a non-merged N-cycle*/
			CLOCK_I();
			promote_fetch_to_n = PROMOTE_NOMERGE;
		}
		else
			tsc += (last_cycle_length - cyc_i);
	}
	else if (cp15_cacheon)
		CLOCK_I();   /* + 1I*/
}

/*Reset the clock domains, the DMA timers and the ARM3 cache. Called from
  resetarm(), which used to do this inline.*/
void arm_timing_reset(void)
{
	memset(arm3_cache, 0, sizeof(arm3_cache));
	memset(arm3_cache_tag, TAG_INVALID, sizeof(arm3_cache_tag));

	tsc = 0;
	mem_available_ts = 0;
	refresh_ts = 0;
	clock_domain = DOMAIN_MCLK;
	pending_reads = 0;
	promote_fetch_to_n = PROMOTE_NONE;
}
