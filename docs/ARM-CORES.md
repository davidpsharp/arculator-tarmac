# CPU cores

Arculator's ARM emulation used to be one file, `arm.c`, holding two separate
things: how long each memory access takes, and what each instruction does.
This fork separates them, so that a second interpreter can be added without
disturbing the first — and so that one binary can run either, chosen at run
time.

| File | What |
|---|---|
| `src/arm_timing.c` | Clock domains (FCLK/MCLK/IOCLK), DMA arbitration for refresh, sound, cursor and video, the ARM3 cache and its timing. Shared by every core. |
| `src/arm.c` | Arculator's interpreter: decode, execute, exceptions, the run loop. |
| `src/arm_core.c` | The public `resetarm()`/`execarm()`/`dumpregs()`, dispatching to the selected core. |
| `src/arm_timing.h`, `src/arm_core.h` | The two interfaces, and what a core must do. |

## Selecting a core

The machine setting `cpu_core` (in `configs/<name>.cfg`, default
`arculator`) names the core. Selection happens in `arc_set_cpu()`, so it
takes effect on reset — as changing the CPU type already does — and an
unknown name falls back to the first core and says so in the log rather than
refusing to start.

The indirection is free: `exec()` is called once per 1/100 s and runs the
whole inner loop itself, so there is one indirect call per 10 ms of emulated
time rather than one per instruction. Measured: `!ARMTest` takes 12.2 s
either way.

## What a core has to do

`src/arm_core.h` has the full contract; the part that constrains a new core
most is that the architectural state is **not private to it**. `armregs[]`
is read and written directly by `fpa.c` (which does the FPA's arithmetic on
`armregs[RD]`), by `hostfs.c` and `hostcmd.c` (SWI arguments), by
`keyboard.c`'s mouse hack, and by the debugger. So a core must keep the
current mode's registers in `armregs[]`, R15 holding PC and PSR in the
26-bit layout, and have them up to date whenever it calls out — SWI, FPA,
abort — or returns from `exec()`.

The same applies to `databort` (1 = data abort, 2 = address exception, set
by `mem.c`), `prefabort`, `irq` (IOC's interrupt state), `memmode` (drives
`mem.c`'s access checks; a core changes it for LDRT/STRT and for SWI
handlers needing supervisor access) and `ins` (the MIPS display).

A core is also expected to call the timing model in `arm_timing.h`
(`cache_read_timing`, `cache_write_timing`, `merge_timing`, `arm_clock_i`)
so that sound, video, IOC timers and podules stay in step. A core that does
not will run, but everything scheduled against `tsc` will be wrong.

## Why run-time rather than build-time

Being able to run the same binary on either core is what makes a comparison
mean anything: the same `!ARMTest` run (`tools/armtest.ps1`), the same
instruction trace (`docs/DEBUGSOCK.md`), the same ROM, the same timing
model — everything identical except the interpreter. A build-time choice
would give two binaries that differ in ways nobody had accounted for.

It also matters for a release: a user can see the difference for themselves
by changing one line in a configuration file.
