#!/usr/bin/env bash
# Prueba de integracion del plugin Linux contra libobs REAL (Debian trixie, libobs 30.2.3 / libobs.so.0),
# sin interfaz grafica. Descarga libobs y sus dependencias que falten (sin apt, sin root).
#   scripts/run-obs-integration.sh <plugin_id> [prueba.c]
#   (por defecto tests/obs-integration/obs_load_test.c, la prueba del Eco Ping-Pong)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ID="${1:-streamerdesk-asmr-pingpong-echo}"
TEST_SRC="${2:-$ROOT/tests/obs-integration/obs_load_test.c}"
DEPS="${DEPS:-$ROOT/.deps}"
SYSROOT="$DEPS/sysroot"
SO="$ROOT/build/$ID/linux/$ID.so"
DATA="${OBS_TEST_DATA:-$ROOT/build/$ID/stage-linux/$ID/data}"
if [ ! -e "$SYSROOT/usr/lib/x86_64-linux-gnu/libobs.so.0" ]; then
	mkdir -p "$DEPS/sysroot-dl" && cd "$DEPS/sysroot-dl"
	[ -f Packages ] || { curl -fsSL -o Packages.xz https://deb.debian.org/debian/dists/trixie/main/binary-amd64/Packages.xz && xz -dk Packages.xz; }
	python3 "$ROOT/scripts/debian_resolve.py" libobs0t64 >files.txt
	while read -r f; do curl -fsSLO "https://deb.debian.org/debian/$f"; done <files.txt
	for f in *.deb; do dpkg-deb -x "$f" "$SYSROOT"; done
	cd "$ROOT"
fi
LIB="$SYSROOT/usr/lib/x86_64-linux-gnu"
gcc -O1 -std=gnu11 -o "$ROOT/build/$ID/obs_load_test" "$TEST_SRC" \
	-I"$DEPS/obs-headers/libobs" -I"$DEPS/config" "$LIB/libobs.so.0" -lm -lpthread
CFG="$(mktemp -d)"
LD_LIBRARY_PATH="$LIB" "$ROOT/build/$ID/obs_load_test" "$SO" "$DATA" "$CFG" 2>&1 |
	grep -v '^debug: ' | tee "$ROOT/build/$ID/obs-integration.log"
rm -rf "$CFG"
