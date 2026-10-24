#!/bin/sh
# Set up the MS-DOS cross toolchain for Dune Dynasty on Linux x86-64.
#
# Installs, without root:
#   $DJGPP_PREFIX            DJGPP cross compiler: the newest build-djgpp
#                            release for Linux x86-64
#   $TOOLS_DIR/src           Allegro 4.2 (last Allegro with DOS support), built
#                            for DJGPP from the head of its 4.2 branch
#   $TOOLS_DIR/dl/cwsdpmi    CWSDPMI, the DPMI host the game runs under: the
#                            newest csdpmi*b.zip from DJGPP's archive
#
# Nothing is pinned. What is already installed is kept; delete it to update.
#
# Usage: scripts/dos/setup-toolchain.sh            install what is missing
#        scripts/dos/setup-toolchain.sh versions   print the versions that
#                                                  would be used (cache key)
# Override locations with DJGPP_PREFIX=... TOOLS_DIR=..., the DJGPP download
# with DJGPP_URL=.... GITHUB_TOKEN, if set, is used for GitHub's API.

set -eu

DJGPP_PREFIX="${DJGPP_PREFIX:-$HOME/djgpp}"
TOOLS_DIR="${TOOLS_DIR:-$HOME/tools}"

DJGPP_RELEASES="https://api.github.com/repos/andrewwutw/build-djgpp/releases/latest"
ALLEGRO_REPO="https://github.com/liballeg/allegro5.git"
ALLEGRO_BRANCH="4.2"
CWSDPMI_SITE="https://www.delorie.com/pub/djgpp/current/v2misc/"

TARGET=i586-pc-msdosdjgpp

github_api() {
	if [ -n "${GITHUB_TOKEN:-}" ]; then
		curl -fsSL -H "Authorization: Bearer $GITHUB_TOKEN" "$1"
	else
		curl -fsSL "$1"
	fi
}

if [ -z "${DJGPP_URL:-}" ]; then
	DJGPP_URL=$(github_api "$DJGPP_RELEASES" |
		grep -o '"browser_download_url": *"[^"]*/djgpp-linux64-gcc[0-9]*\.tar\.bz2"' |
		sed 's/.*"\(https[^"]*\)"/\1/' | head -n 1)
	[ -n "$DJGPP_URL" ] || { echo "No Linux x86-64 build-djgpp release found" >&2; exit 1; }
fi
ALLEGRO_COMMIT=$(git ls-remote "$ALLEGRO_REPO" "refs/heads/$ALLEGRO_BRANCH" | cut -c1-12)
[ -n "$ALLEGRO_COMMIT" ] || { echo "No $ALLEGRO_BRANCH branch in $ALLEGRO_REPO" >&2; exit 1; }
CWSDPMI_ZIP=$(curl -fsSL "$CWSDPMI_SITE" | grep -o -E 'csdpmi[0-9]+b\.zip' |
	sort -u -V | tail -n 1)
[ -n "$CWSDPMI_ZIP" ] || { echo "No csdpmi*b.zip in $CWSDPMI_SITE" >&2; exit 1; }

if [ "${1:-}" = versions ]; then
	# e.g. djgpp-v3.4-gcc1220_allegro-a2939b8f50ab_cwsdpmi-csdpmi7b
	echo "djgpp-$(echo "$DJGPP_URL" | sed 's|.*/download/\([^/]*\)/djgpp-linux64-\(gcc[0-9]*\).*|\1-\2|')_allegro-${ALLEGRO_COMMIT}_cwsdpmi-${CWSDPMI_ZIP%.zip}"
	exit 0
fi

mkdir -p "$TOOLS_DIR/dl" "$TOOLS_DIR/src"

# ---- DJGPP ----------------------------------------------------------------
if [ ! -x "$DJGPP_PREFIX/bin/$TARGET-gcc" ]; then
	echo "Installing DJGPP to $DJGPP_PREFIX from $DJGPP_URL"
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
	echo "Building Allegro $ALLEGRO_BRANCH ($ALLEGRO_COMMIT) for DJGPP in $ALLEGRO_DIR"
	if [ ! -d "$ALLEGRO_DIR" ]; then
		git clone -q -b "$ALLEGRO_BRANCH" "$ALLEGRO_REPO" "$ALLEGRO_DIR"
	else
		git -C "$ALLEGRO_DIR" fetch -q origin "$ALLEGRO_BRANCH"
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

# ---- CWSDPMI --------------------------------------------------------------
CWSDPMI_DIR="$TOOLS_DIR/dl/cwsdpmi"
if [ ! -f "$CWSDPMI_DIR/bin/CWSDPMI.EXE" ]; then
	echo "Installing CWSDPMI ($CWSDPMI_ZIP) to $CWSDPMI_DIR"
	mkdir -p "$CWSDPMI_DIR"
	curl -fsSL -o "$CWSDPMI_DIR/$CWSDPMI_ZIP" "$CWSDPMI_SITE$CWSDPMI_ZIP"
	python3 -I -m zipfile -e "$CWSDPMI_DIR/$CWSDPMI_ZIP" "$CWSDPMI_DIR"
fi

echo
echo "DJGPP:       $DJGPP_PREFIX"
echo "Allegro 4.2: $ALLEGRO_DIR"
echo "CWSDPMI:     $CWSDPMI_DIR/bin"
echo
echo "Configure with:"
echo "  cmake -S . -B build-dos -DCMAKE_TOOLCHAIN_FILE=cmake/djgpp.cmake -DALLEGRO4_DIR=$ALLEGRO_DIR"
