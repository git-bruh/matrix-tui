#!/bin/sh -e

# Clang supports -fsanitize-blacklist which helps us suppress false positives
export CC=clang
# Remove optimization flags
export CFLAGS="$(echo "$CFLAGS" | sed 's|-O[0-9]||g')"

rm -rf build

if [ "$TSAN" = 1 ]; then
	sanitize=thread
else
	sanitize=address,undefined
fi

meson \
	--buildtype=debug \
	-Dtests=true \
	-Doptimization=1 \
	-Db_sanitize="$sanitize" \
	-Db_coverage=true \
	-Db_lto=false \
	"$@" \
	build
