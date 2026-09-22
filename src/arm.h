#include "arm_timing.h"	/* the timing half of the CPU emulation */

extern int arm_cpu_type;

extern int arm_cpu_speed, arm_mem_speed;
extern int arm_has_swp;
extern int arm_has_cp15;

#define ARM_USER_MODE (!(armregs[15] & 3))

extern uint32_t rotatelookup[4096];
