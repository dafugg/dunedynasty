#!/bin/bash -e
#
# MSYS2 no longer provides 32-bit (MINGW32) packages for allegro, enet and
# libmad. Build them from MSYS2's own recipes (MINGW-packages), pinned to
# one commit, and install them. Run in a MINGW32 shell with base-devel,
# git and the dependencies installed (see windows-x86-build.yml).
#
# Usage: scripts/build-windows-x86-deps.sh <output dir for packages>

MINGW_PACKAGES_COMMIT=0af9837983fa49907500f33b50ea231498185d05
PACKAGES="libmad enet allegro"

OUT=$(realpath -m "${1:-deps-x86}")
mkdir -p "$OUT"

if ! ls "$OUT"/*.pkg.tar.zst >/dev/null 2>&1; then
	SRC=$(mktemp -d)
	git -C "$SRC" init -q
	git -C "$SRC" remote add origin https://github.com/msys2/MINGW-packages.git
	git -C "$SRC" sparse-checkout set $(printf 'mingw-w64-%s ' $PACKAGES)
	git -C "$SRC" fetch -q --depth 1 origin "$MINGW_PACKAGES_COMMIT"
	git -C "$SRC" checkout -q FETCH_HEAD

	for pkg in $PACKAGES; do
		dir="$SRC/mingw-w64-$pkg"

		# The recipes no longer list mingw32.
		sed -i "s/^mingw_arch=(/mingw_arch=('mingw32' /" "$dir/PKGBUILD"

		# Allegro add-ons whose libraries MINGW32 no longer has, and which
		# Dune Dynasty does not use: Allegro's CMake leaves them out.
		if [ "$pkg" = allegro ]; then
			sed -i -E '/-(dumb|libtheora|openal|physfs)"/d' "$dir/PKGBUILD"
		fi

		(cd "$dir" && MINGW_ARCH=mingw32 makepkg-mingw --syncdeps --noconfirm --cleanbuild --force)
		cp "$dir"/*.pkg.tar.zst "$OUT"/
	done
fi

pacman -U --noconfirm "$OUT"/*.pkg.tar.zst
