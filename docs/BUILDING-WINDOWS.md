# Building on Windows (MSYS2 / MinGW-w64)

This fork builds with the stock autotools scripts under MSYS2's MINGW64
environment. Tested with gcc 16.2, wxWidgets 3.2.11 (MSYS2 package, built with
`wxUSE_STL`), SDL2 2.32, on Windows 11.

1. Install MSYS2 (`winget install MSYS2.MSYS2`) and open a **MINGW64** shell.
2. Install the packages:

       pacman -S --needed base-devel autoconf automake libtool make pkgconf \
         mingw-w64-x86_64-toolchain mingw-w64-x86_64-wxwidgets3.2-msw \
         mingw-w64-x86_64-SDL2 mingw-w64-x86_64-zlib mingw-w64-x86_64-pkgconf

3. Build and stage:

       ./build-mingw.sh --stage /c/path/to/arculator-data

   `build-mingw.sh` runs `autoreconf -fi` the first time (the repository's
   `config.sub`/`config.guess` are symlinks that do not survive a Windows
   checkout), then `configure --enable-release-build`, `make`, and copies
   `arculator.exe` plus the MinGW DLLs it depends on into the stage directory.
   The stage directory must also contain `roms/`, `cmos/`, `configs/`,
   `ddnoise/`, `hostfs/` and `arc.cfg` — take them from an Arculator release.

4. Run: `arculator.exe A5000` starts the named configuration directly,
   skipping the configuration manager.

## Source changes needed for this toolchain

- `src/Makefile.am`: `-std=gnu17` for the C sources. gcc 15+ defaults to C23,
  where `void f();` means `void f(void)` and clashes with the later
  `void f(ide_t *)` definitions in `ide_hccs_a3k.c`.
- `src/wx-config.cc`, `wx-console.cc`, `wx-hd_conf.cc`, `wx-hd_new.cc`: explicit
  `.mb_str()` where a `wxString` was passed to `atoi`/`strcpy`/`strncpy`. The
  MSYS2 wxWidgets is built with `wxUSE_STL=1`, which removes wxString's
  implicit `const char*` conversion.

Podule plug-ins (`podules/*`) are not built by this script.
