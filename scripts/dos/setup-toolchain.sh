#!/bin/sh
# Set up the MS-DOS cross toolchain for Dune Dynasty on Linux x86-64.
#
# Installs, without root:
#   $DJGPP_PREFIX         DJGPP gcc 12.2 cross compiler (build-djgpp v3.4)
#   $TOOLS_DIR/src        Allegro 4.2 (last Allegro with DOS support), built for DJGPP
#
# Usage: scripts/dos/setup-toolchain.sh
# Override locations with DJGPP_PREFIX=... TOOLS_DIR=...

set -eu

DJGPP_PREFIX="${DJGPP_PREFIX:-$HOME/djgpp}"
TOOLS_DIR="${TOOLS_DIR:-$HOME/tools}"

DJGPP_URL="https://github.com/andrewwutw/build-djgpp/releases/download/v3.4/djgpp-linux64-gcc1220.tar.bz2"
ALLEGRO_REPO="https://github.com/liballeg/allegro5.git"
ALLEGRO_BRANCH="4.2"
ALLEGRO_COMMIT="a2939b8"

TARGET=i586-pc-msdosdjgpp

mkdir -p "$TOOLS_DIR/dl" "$TOOLS_DIR/src"

# ---- DJGPP ----------------------------------------------------------------
if [ ! -x "$DJGPP_PREFIX/bin/$TARGET-gcc" ]; then
	echo "Installing DJGPP to $DJGPP_PREFIX"
	curl -sSL -o "$TOOLS_DIR/dl/djgpp.tar.bz2" "$DJGPP_URL"
	mkdir -p "$DJGPP_PREFIX"
	tar xjf "$TOOLS_DIR/dl/djgpp.tar.bz2" -C "$DJGPP_PREFIX" --strip-components=1
fi

# The prebuilt binutils ar/ranlib are linked against the flex runtime
# (libfl.so.2), which is rarely installed.  Bundle it and wrap the tools.
if ! "$DJGPP_PREFIX/bin/$TARGET-ar" --version >/dev/null 2>&1; then
	echo "Bundling libfl.so.2 for $TARGET-ar/ranlib"
	tmp=$(mktemp -d)
	(cd "$tmp" && apt download libfl2 >/dev/null && dpkg-deb -x libfl2_*.deb x)
	mkdir -p "$DJGPP_PREFIX/hostlib"
	cp -a "$tmp"/x/usr/lib/x86_64-linux-gnu/libfl.so.2* "$DJGPP_PREFIX/hostlib/"
	rm -rf "$tmp"

	for t in ar ranlib; do
		mv "$DJGPP_PREFIX/bin/$TARGET-$t" "$DJGPP_PREFIX/bin/$TARGET-$t.real"
		cat > "$DJGPP_PREFIX/bin/$TARGET-$t" <<EOF
#!/bin/sh
# libfl.so.2 is not installed system-wide; ship it with the toolchain.
LD_LIBRARY_PATH="$DJGPP_PREFIX/hostlib\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}" exec "$DJGPP_PREFIX/bin/$TARGET-$t.real" "\$@"
EOF
		chmod +x "$DJGPP_PREFIX/bin/$TARGET-$t"
		rm -f "$DJGPP_PREFIX/$TARGET/bin/$t"
		ln -s "../../bin/$TARGET-$t" "$DJGPP_PREFIX/$TARGET/bin/$t"
	done
fi

# ---- Allegro 4.2 ----------------------------------------------------------
ALLEGRO_DIR="$TOOLS_DIR/src/allegro-4.2"
if [ ! -f "$ALLEGRO_DIR/lib/djgpp/liballeg.a" ]; then
	echo "Building Allegro $ALLEGRO_BRANCH for DJGPP in $ALLEGRO_DIR"
	if [ ! -d "$ALLEGRO_DIR" ]; then
		git clone -q -b "$ALLEGRO_BRANCH" "$ALLEGRO_REPO" "$ALLEGRO_DIR"
	fi
	(
		cd "$ALLEGRO_DIR"
		git checkout -q "$ALLEGRO_COMMIT"
		./fix.sh djgpp --quick >/dev/null

		PATH="$DJGPP_PREFIX/bin:$DJGPP_PREFIX/$TARGET/bin:$PATH"
		export PATH
		set -- CROSSCOMPILE=1 DJDIR="$DJGPP_PREFIX/$TARGET" CC=$TARGET-gcc

		# Generated headers first; the makefile races on them under -j.
		make "$@" obj/djgpp/asmcapa.h obj/djgpp/asmdef.inc
		make "$@" lib -j"$(nproc)"
	)
fi

echo
echo "DJGPP:       $DJGPP_PREFIX"
echo "Allegro 4.2: $ALLEGRO_DIR"
echo
echo "Configure with:"
echo "  cmake -S . -B build-dos -DCMAKE_TOOLCHAIN_FILE=cmake/djgpp.cmake -DALLEGRO4_DIR=$ALLEGRO_DIR"
