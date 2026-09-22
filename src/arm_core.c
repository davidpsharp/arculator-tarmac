/*
  Arculator - Acorn Archimedes emulator

  CPU core selection - see arm_core.h.

  The public entry points (resetarm, execarm, dumpregs) live here and
  dispatch to the selected core, so that nothing outside has to know which
  core is running.
 */

#include <string.h>

#include "arc.h"
#include "arm.h"
#include "arm_core.h"

/* Arculator's own interpreter, in arm.c. */
extern void arm_interp_reset(void);
extern void arm_interp_exec(int cycles);
extern void arm_interp_dumpregs(void);

static const arm_core_t arculator_core =
{
	.id       = "arculator",
	.name     = "Arculator",
	.reset    = arm_interp_reset,
	.exec     = arm_interp_exec,
	.dumpregs = arm_interp_dumpregs,
};

const arm_core_t *arm_cores[] =
{
	&arculator_core,
	NULL
};

const arm_core_t *arm_core = &arculator_core;

char arm_core_id[32] = "arculator";

void arm_core_select(const char *id)
{
	int c;

	if (id == NULL || id[0] == '\0') {
		id = arm_cores[0]->id;
	}

	for (c = 0; arm_cores[c] != NULL; c++) {
		if (!strcasecmp(arm_cores[c]->id, id)) {
			if (arm_core != arm_cores[c]) {
				rpclog("arm_core_select: CPU core is %s\n",
				    arm_cores[c]->name);
			}
			arm_core = arm_cores[c];
			strncpy(arm_core_id, arm_cores[c]->id,
			    sizeof(arm_core_id) - 1);
			arm_core_id[sizeof(arm_core_id) - 1] = '\0';
			return;
		}
	}

	/* An unknown core in a configuration file is a typo or a machine moved
	   from a build that had one this build has not. Say so and carry on
	   with the default rather than refusing to start. */
	rpclog("arm_core_select: no CPU core called '%s', using %s\n",
	    id, arm_cores[0]->name);
	arm_core = arm_cores[0];
	strncpy(arm_core_id, arm_cores[0]->id, sizeof(arm_core_id) - 1);
	arm_core_id[sizeof(arm_core_id) - 1] = '\0';
}

const char *arm_core_name(void)
{
	return arm_core->name;
}

/* ---- the public entry points ----------------------------------------- */

void resetarm(void)
{
	arm_core->reset();
}

void execarm(int cycles)
{
	arm_core->exec(cycles);
}

void dumpregs(void)
{
	arm_core->dumpregs();
}
