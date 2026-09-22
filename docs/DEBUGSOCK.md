# DebugSock — drive Arculator's debugger from the host

Arculator has a good built-in debugger (breakpoints, watchpoints, stepping,
disassembly, memory and device registers, exception traps), but it is only
reachable by typing in its console window. DebugSock puts the same debugger
on a local TCP socket so it can be scripted, and adds an **instruction
trace** for comparing two CPU cores running the same software.

Nothing in the debugger itself changed, so the console window still works
exactly as before and offers the same commands.

| Piece | Where |
|---|---|
| Socket front end | `src/debugsock.c`, `src/debugsock.h` |
| Two hooks in the debugger | `src/debugger.c`: `debug_out()` mirrors to the socket; `debugger_input_get()` prefers a connected client over the console |
| Instruction trace | `src/debugger.c` (`trace` command, `debug_trace_instruction()`), one call in `src/arm.c` inside the existing `if (debugon)` |
| Client | `tools/arc-debug.pl` |
| Trace comparison | `tools/tracediff.pl` |

## Configuration

Per machine, in `configs/<name>.cfg`:

| Key | Default | Meaning |
|---|---|---|
| `debugsock_enabled` | `1` | Listen for a debugger client. |
| `debugsock_port` | `15601` | TCP port on `127.0.0.1`; the next free port (up to +15) is taken if it is in use, and logged. |

## Using it

```
# one-shot commands
perl tools/arc-debug.pl -- pause r
perl tools/arc-debug.pl -- 'break 1f033ac' c

# interactive
perl tools/arc-debug.pl
```

Each argument after `--` is one debugger command line. `h` lists them all.
Useful ones: `r` (registers; `r ioc`, `r memc`, `r memc_cam`, `r vidc`),
`d [addr]` (disassemble), `m`/`mb [addr]` (memory), `s [n]` (step),
`c` (continue), `break`/`breakw`/`watchw` and `blist`/`wlist`/`bclear`,
`t enable dataabort` (and `prefabort`, `addrexcep`, `undefins`, `swi`),
`write`/`writeb`, `save <fn> <addr> <size>`.

Things to know:

- **The socket is checked before the console.** While a client is connected
  the console window is not consulted, so the debugger works with no console
  window open — which is what makes it scriptable.
- **Closing the client resumes the machine.** That is deliberate: a script
  that dies should not leave the emulator frozen. It does mean each new
  connection starts with the machine running.
- **While the machine is running** the only commands the socket accepts are
  `pause`, `detach` and `help`; anything else is refused, because the
  debugger is not in its command loop to read it. `arc-debug.pl` notices and
  sends `pause` for you (`[auto-pausing]`), so a script can just say `r`.
- The debugger's output has no framing, so the client treats 400 ms of
  silence as the end of a reply (`--quiet-ms` to change).
- A paused machine stops everything, HostCmd included.

## Instruction trace

```
trace <file> [n]    start tracing to <file>, stopping after n instructions
                    (default 1000000)
trace off           stop
```

One line per instruction, written **before** it executes:

```
<pc> <opcode> <r0> <r1> ... <r14> <r15+psr>
```

all hex, `pc` 7 digits and the rest 8. Nothing that varies between runs of
the same core (timings, cycle counts, host addresses) is included, so two
runs of the same software on the same core produce byte-identical files.

That is the point: when a second CPU core runs the same software, the first
line that differs is the instruction where the two cores part company.

```
perl tools/arc-debug.pl -- pause 'trace C:/tmp/trace-a.txt 200000' c
# ... swap cores, repeat as trace-b.txt ...
perl tools/tracediff.pl C:/tmp/trace-a.txt C:/tmp/trace-b.txt
```

`tracediff.pl` prints the instruction number and PC where they diverge, the
preceding instructions for context (`--context N`), both lines, and which
registers disagree. Exit code 0 identical, 1 diverged, 2 unreadable.
`--skip N` ignores the first N instructions, for when an early, known and
harmless difference (such as an uninitialised register) would otherwise stop
the comparison.

Cost: the trace is tested once per instruction inside the `if (debugon)`
that the debugger already has, so a machine running without the debugger
enabled pays nothing at all.

## Why not RPCEmu-extended's debugger

RPCEmu-extended has a much larger debugger (conditional breakpoints with an
expression language, symbols, backtraces, a JSON control socket, ~8000
lines). It is built around RPCEmu's 32-bit CPU and MMU and adds hooks on the
hot paths, and its strengths are aimed at debugging RISC OS software rather
than at comparing two CPU cores. The trace above answers the question this
fork actually asks — "which instruction first behaved differently?" — in one
run, which no interactive debugger does. If an expression language for
conditional breakpoints is wanted later, RPCEmu-extended's `debugexpr.c` is
self-contained and could be adopted on its own.
