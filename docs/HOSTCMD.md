# HostCmd — drive the emulated RISC OS command line from the host

HostCmd lets a program on the host run `*` commands inside the emulated
machine and read back their output and return code. It exists so the
Tarmac work can be tested without keyboard automation: `!ARMTest` (and any
other guest command) is launched from a script, its output streams back to
the host, and the exit status says whether it ran.

It is RPCEmu-extended's HostCmd (Andy Timmins, GPL v2) with the emulator half
ported to Arculator and the guest half reused unchanged:

| Piece | Where | Origin |
|---|---|---|
| Emulator side: SWI handler + loopback TCP server | `src/hostcmd.c`, `src/hostcmd.h`, `src/socket-compat.h` | RPCEmu-extended `src/hostcmd.c`, adapted (TCP only, Arculator config/logging) |
| SWI dispatch | `src/arm.c` (`opSWI`), `ARCEM_SWI_HOSTCMD` = `&56AC5` in `src/hostfs.h` | same chunk as HostFS (`&56AC0`), sub-op in R9 |
| Guest gateway module `RPCEmuSupport` 0.03 | `src/hostfs/rpcemusupport/` (source + binary), built into `roms/arcrom_ext` | RPCEmu-extended `riscos-progs/RPCEmuSupport`, binary identical to the 2.0.0 release |
| Extension ROM builder | `tools/mkextrom.pl` | new; reproduces the shipped `arcrom_ext` byte for byte before the module was added |
| Client | `rpcemu-run.exe` / `rpcemu-shell.exe` from an RPCEmu-extended release, with `--tcp` | unchanged |

The module runs on RISC OS 3.11 / ARM3 (it is 26/32-bit neutral, ARMv3
floor, no MRS/MSR). It announces itself over the SWI at boot, polls for
commands from a 1 cs ticker, runs each through `OS_CLI` from a transient
callback with `WrchV` claimed (or, once the desktop is up, in a `TaskWindow`
child so commands that need an application slot work), and streams the
captured output back.

## Configuration

Per machine, in `configs/<name>.cfg`:

| Key | Default | Meaning |
|---|---|---|
| `hostcmd_enabled` | `1` | Listen for a host client. |
| `hostcmd_port` | `15600` | TCP port on `127.0.0.1`. If it is in use the next free port (up to +15) is taken and logged (`HostCmd: endpoint 127.0.0.1:NNNNN` in the log). RPCEmu-extended's own default is 15590, so both emulators can run at once. |

Both keys are written back to the `.cfg` when Arculator saves it, so they
appear in every machine's file after its first run.

## Using it

```
rpcemu-run.exe --tcp 127.0.0.1:15600 -- Modules
rpcemu-run.exe --tcp 127.0.0.1:15600 -- "Set ARMTest$NoWait 1"
rpcemu-run.exe --tcp 127.0.0.1:15600 -- "Run HostFS::HostFS.$.!ARMTest"
```

- One command at a time; the RISC OS session (CSD, system variables)
  persists between calls and across a guest reset.
- The exit code of `rpcemu-run` is the guest's `Sys$ReturnCode`; `-1` means
  the command never ran (no module, machine reset, or the module stopped
  polling for 15 s).
- No stdin: commands that prompt will hang until the client's timeout. Use
  `BASIC -quit <file>`, `Obey` files, etc.
- Commands that need an application slot (`WimpSlot`, `BASIC`) work once the
  desktop is running (the TaskWindow path); before that the module runs
  them in the CLI context with what memory the supervisor has.

Wire protocol (for writing another client): the client sends one
`\n`-terminated command line; the server replies with frames
`[type:1][len:u32 BE][payload]` where type is `O` (output), `D` (done,
payload = 4-byte BE return code) or `X` (advisory text, e.g. the greeting
`Arculator HostCmd v1 (running on <config>)`).

## Rebuilding the extension ROM

`roms/arcrom_ext` is a 64 KB Acorn extension ROM holding HostFS,
HostFSFiler and RPCEmuSupport. To rebuild it after changing a module:

```
perl tools/mkextrom.pl -o roms/arcrom_ext \
    src/hostfs/hostfs,ffa src/hostfs/hostfsfiler,ffa \
    src/hostfs/rpcemusupport/rpcemusupport,ffa
```

The module itself is assembled with GNU binutils for ARM
(`src/hostfs/rpcemusupport/Makefile`, see `src/hostfs/arm_binutils.txt`);
the committed binary is the one RPCEmu-extended ships.

## Booting straight into a test

RISC OS 3.11 will boot from HostFS: at the `*` prompt (F12)
`*Configure FileSystem HostFS` and `*Configure Boot`, then reset. The
`!Boot` in the HostFS root runs (`Boot$Dir` = `HostFS::HostFS.$.!Boot`), so a
test configuration can start `!ARMTest` on every launch without any host
involvement. The setting lives in that machine's CMOS file under `cmos/`.
