#!/bin/bash
# Build Arculator on Windows with MSYS2 MinGW-w64 and stage a runnable tree.
#
# Run from an MSYS2 MINGW64 shell (or: MSYSTEM=MINGW64 bash -l build-mingw.sh).
# Prerequisites (pacman -S --needed ...):
#   base-devel autoconf automake libtool make pkgconf
#   mingw-w64-x86_64-toolchain mingw-w64-x86_64-wxwidgets3.2-msw
#   mingw-w64-x86_64-SDL2 mingw-w64-x86_64-zlib mingw-w64-x86_64-pkgconf
#
# Usage: ./build-mingw.sh [--clean] [--stage DIR]
#   --clean      make clean before building
#   --stage DIR  copy arculator.exe and the MinGW DLLs it needs into DIR
#                (default: ./build-mingw). ROMs, cmos, configs and hostfs are
#                NOT copied; point DIR at an existing Arculator data tree or
#                copy them in yourself.
set -e
cd "$(dirname "$0")"

CLEAN=0
STAGE=./build-mingw
while [ $# -gt 0 ]; do
	case "$1" in
		--clean) CLEAN=1 ;;
		--stage) STAGE="$2"; shift ;;
		*) echo "unknown option $1"; exit 2 ;;
	esac
	shift
done

# The repository tracks config.sub/config.guess as symlinks into an automake
# install that does not exist here; regenerate the autotools output once.
if [ ! -f configure ] || [ ! -s config.sub ] || head -c 1 config.sub | grep -q '/'; then
	echo "== autoreconf"
	autoreconf -fi
fi

if [ ! -f src/Makefile ] || [ configure -nt config.status ]; then
	echo "== configure"
	./configure --enable-release-build
fi

[ $CLEAN = 1 ] && make clean

echo "== make"
make -j"$(nproc)"

echo "== stage into $STAGE"
mkdir -p "$STAGE"
cp arculator.exe "$STAGE/"
ldd arculator.exe | awk '/mingw64/ {print $3}' | while read dll; do
	cp -u "$dll" "$STAGE/"
done
echo "done: $STAGE/arculator.exe"
