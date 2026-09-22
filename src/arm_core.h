/*
  Arculator - Acorn Archimedes emulator

  CPU core selection.

  The emulator can be built with more than one ARM interpreter and choose
  between them at run time, so that one binary can run the same software on
  either and the two can be compared directly. Selection is a machine
  configuration setting (cpu_core) and takes effect on reset.

  The indirection costs nothing measurable: exec() is called once per 1/100 s
  and runs the whole inner loop itself, so there is one indirect call per
  10 ms of emulated time, not one per instruction.

  === What a core must do ===

  A core owns the architectural state while it is running, but that state is
  not private to it. armregs[] in particular is read and written directly by
  a dozen other files - fpa.c does arithmetic on armregs[RD], hostfs.c and
  hostcmd.c take SWI arguments from it, keyboard.c's mouse hack writes to it,
  the debugger prints it - so a core must keep the current mode's registers
  in armregs[], with R15 holding PC and PSR in the 26-bit layout, and must
  have them up to date whenever it calls out (SWI handling, FPA, aborts) or
  returns from exec().

  The same applies to the flags the rest of the emulator sets and reads:

    databort    0, 1 = data abort, 2 = address exception; set by mem.c during
                an access, cleared by the core when it has taken the exception
    prefabort   set by the fetch path, taken by the core
    irq         IOC's current interrupt state (bit 0 IRQ, bit 1 FIQ),
                maintained by ioc.c; armirq is the core's sampled copy
    memmode     MEMMODE_USER/OS/SUPER, drives mem.c's access checks; a core
                changes it for LDRT/STRT and for the SWI handlers that need
                supervisor access
    ins         instructions executed, used for the MIPS display
    osmode      set from MEMC control

  A core is also expected to call the timing model (see arm_timing.h) so that
  sound, video, IOC timers and podules stay in step; a core that does not
  will run, but everything scheduled against tsc will be wrong.
 */

#ifndef ARM_CORE_H
#define ARM_CORE_H

typedef struct arm_core_t
{
	const char *id;		/**< value of the cpu_core config key */
	const char *name;	/**< shown in the UI */

	void (*reset)(void);		/**< reset to the power-on state */
	void (*exec)(int cycles);	/**< run for this many cycles */
	void (*dumpregs)(void);		/**< write the register state to a log */
} arm_core_t;

/* The core currently selected. Never NULL after arm_core_init(). */
extern const arm_core_t *arm_core;

/* All cores this build has, NULL terminated. */
extern const arm_core_t *arm_cores[];

/* Select by config id; unknown ids fall back to the first core and log it.
   Takes effect at the next reset, which is when arc_set_cpu() calls it. */
void arm_core_select(const char *id);

/* Name of the selected core, for the log and the UI. */
const char *arm_core_name(void);

/* Configuration: the cpu_core key, read by config.c. */
extern char arm_core_id[32];

#endif /* ARM_CORE_H */
