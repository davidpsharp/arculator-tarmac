# Arculator (arculator-tarmac fork)

Arculator is an Acorn Archimedes emulator originally written by [Sarah Walker](https://github.com/sarah-walker-pcem). It emulates the Acorn Archimedes series of computers, including models like the A3000, A3010, A3020, A4000, A5000, and more.

This is David Sharp's fork, made to host the **Tarmac** ARM2/ARM3 CPU core
(written in 2001 as a final-year project and revived in 2026) as a selectable
replacement for Arculator's own, so it can be released and tested inside a
complete, redistributable emulator. It builds on
[jankfactor's CMake fork](https://github.com/jankfactor/arculator) for the
CMake build system and macOS support, and tracks Sarah Walker's upstream.

What differs from upstream so far (branch `tarmac`):

- **HostCmd** — run RISC OS `*commands` from the host over a local socket and
  read back their output and return code; ported from RPCEmu-extended. See
  [docs/HOSTCMD.md](docs/HOSTCMD.md).
- `tools/arc-run.pl` (HostCmd client), `tools/armtest.ps1` (runs the
  `!ARMTest` ARM instruction-set soak test end to end and reports the FAIL
  count), `tools/mkextrom.pl` (rebuilds `roms/arcrom_ext`).
- The RPCEmuSupport guest module added to the Arculator support extension ROM.

The Tarmac core itself is not in yet. This fork is developed with AI
assistance and is not intended for submission upstream.

⚠️ **The CMake build system comes from jankfactor's fork and is still settling.** ⚠️

## Building

Arculator uses CMake as its build system.

- On Linux, dependencies are resolved from system packages by default (`ARCULATOR_BUNDLE_DEPENDENCIES=OFF`).
- On Windows/macOS, dependencies are bundled from source by default (`ARCULATOR_BUNDLE_DEPENDENCIES=ON`).

### Prerequisites

- CMake 3.20 or later
- A C/C++ compiler:
  - **Linux/macOS**: GCC or Clang
  - **Windows**: MSYS2 with MinGW64 or UCRT64 toolchain (MSVC is **not** supported)
- Ninja (recommended) or Make

### Quick Start

```bash
# Configure with default preset (Release build with Ninja)
cmake --preset default

# Build and install
cmake --build build --target install
```

The executable will be installed into an `install` in the root of this project, with the roms, and podules copied in.

### Platform-Specific Build Guides

- **[Building on macOS/Linux](docs/Building-macOS-Linux.md)** - Guide for building on Unix-like systems
- **[Building on Windows (MSYS2)](docs/Building-Windows-MSYS2.md)** - Guide for building with MinGW64 or UCRT64 toolchains

### Available Presets

| Preset | Description |
|--------|-------------|
| `default` | Release build using Ninja generator |
| `debug` | Debug build with symbols and debug logging |
| `no-podules` | Build without podule plugins |
| `msys2-mingw64` | Build using MSYS2 MINGW64 toolchain (Windows) |
| `msys2-ucrt64` | Build using MSYS2 UCRT64 toolchain (Windows) |

### Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `ARCULATOR_BUILD_PODULES` | ON | Build expansion podule plugins |
| `ARCULATOR_BUNDLE_DEPENDENCIES` | OFF on Linux, ON elsewhere | Use bundled (CPM-fetched) dependencies instead of system libraries |
| `ARCULATOR_USE_WX_MAIN_WINDOW` | OFF | Use the experimental wxWidgets main window instead of SDL2 (non-Windows only) |

## Running

⚠️ Before running Arculator, you'll need appropriate ROM files placed in the `roms/` directory. See the `roms/` subdirectories for details on which ROM files are expected. To enable podules you'll also need podule ROM files placed in the `podules/` directory. You can find both of these in either the Windows or Linux download from the [original Arculator downloads page](https://b-em.bbcmicro.com/arculator/download.html) and just copy `roms` and `podules` folders over. Be sure to merge rather than replace the existing files when copying them over. 

## License

See [COPYING](COPYING) for license information.
