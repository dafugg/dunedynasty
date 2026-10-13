#!/bin/sh
# Cross-compile the Windows x64 desktop build on Linux, for testing on a
# Windows PC (upstream builds it on Windows with MSYS2; see
# scripts/build-windows-x64.sh).
#
# Usage: scripts/windows/build-windows-x64-cross.sh [build-dir] [cmake options...]
# Env:   LLVM_MINGW  llvm-mingw (~/tools/llvm-mingw, from
#                    github.com/mstorsjo/llvm-mingw, a ucrt-ubuntu release)
#        MINGW_DEPS  MSYS2 UCRT64 packages (~/tools/mingw-ucrt64, from
#                    scripts/windows/fetch-msys2-deps.py)
#        PKGCONF     a pkg-config for the build host (pkg-config, or pkgconf)
# Output: <build-dir>/dist with dunedynasty.exe and the DLLs it needs.

set -eu

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BUILD="${1:-$ROOT/build-win64}"
[ $# -gt 0 ] && shift
LLVM_MINGW="${LLVM_MINGW:-$HOME/tools/llvm-mingw}"
MINGW_DEPS="${MINGW_DEPS:-$HOME/tools/mingw-ucrt64}"
CMAKE="${CMAKE:-cmake}"
command -v "$CMAKE" >/dev/null 2>&1 || CMAKE="$HOME/tools/cmake/bin/cmake"
PKGCONF="${PKGCONF:-$(command -v pkg-config || command -v pkgconf || echo "$HOME/tools/hostpkgs/root/usr/bin/pkgconf")}"
export LLVM_MINGW MINGW_DEPS

mkdir -p "$BUILD"

# pkg-config that only sees the Windows libraries (their .pc files say
# prefix=/ucrt64).
cat > "$BUILD/pkg-config-win64" <<WRAP
#!/bin/sh
PKG_CONFIG_LIBDIR="$MINGW_DEPS/ucrt64/lib/pkgconfig" \\
PKG_CONFIG_SYSROOT_DIR="$MINGW_DEPS" \\
LD_LIBRARY_PATH="$HOME/tools/hostpkgs/root/usr/lib/x86_64-linux-gnu\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}" \\
exec "$PKGCONF" "\$@"
WRAP
chmod +x "$BUILD/pkg-config-win64"

"$CMAKE" -S "$ROOT" -B "$BUILD" \
	-DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/mingw-llvm.cmake" \
	-DCMAKE_BUILD_TYPE=Release \
	-DPKG_CONFIG_EXECUTABLE="$BUILD/pkg-config-win64" "$@"
"$CMAKE" --build "$BUILD" -j"$(nproc)"

# Bundle every DLL the exe imports, recursively, from the MSYS2 tree and
# llvm-mingw's runtime (libc++, libunwind).
OBJDUMP="$LLVM_MINGW/bin/x86_64-w64-mingw32-objdump"
DIST="$BUILD/dist"
todo="$DIST/dunedynasty.exe"
while [ -n "$todo" ]; do
	next=""
	for f in $todo; do
		for dll in $("$OBJDUMP" -p "$f" | sed -n 's/^\s*DLL Name: //p'); do
			[ -e "$DIST/$dll" ] && continue
			for dir in "$MINGW_DEPS/ucrt64/bin" "$LLVM_MINGW/x86_64-w64-mingw32/bin"; do
				if [ -e "$dir/$dll" ]; then
					cp "$dir/$dll" "$DIST/"
					next="$next $DIST/$dll"
					break
				fi
			done
		done
	done
	todo="$next"
done

echo "Built $DIST/dunedynasty.exe with $(ls "$DIST"/*.dll | wc -l) DLLs"
