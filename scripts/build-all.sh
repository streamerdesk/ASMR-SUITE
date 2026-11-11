#!/usr/bin/env bash
# Streamerdesk ASMR Suite - compila y empaqueta un plugin para Windows, macOS y Linux
# desde un solo equipo Linux usando Zig (clang) como compilador cruzado.
#
#   scripts/build-all.sh plugins/pingpong-echo
#
# Requisitos: python3 (+ pip: ziglang pefile macholib pillow), git, curl, tar, ar.
# makensis (NSIS) opcional: si no esta en PATH se descarga de Debian.
# Variables: DEPS (cache de dependencias, por defecto .deps), OUT (por defecto dist),
#   SD_PLATFORMS (por defecto PLUGIN_PLATFORMS de plugin.env, o "windows macos linux").
#   Linux se compila siempre que RUN_OBS_TEST=1 (para la prueba con libobs real), pero solo
#   se empaqueta si esta en la lista de plataformas.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PLUGIN_DIR="$(cd "${1:?uso: build-all.sh plugins/<plugin>}" && pwd)"
source "$ROOT/suite.env"
source "$PLUGIN_DIR/plugin.env"
# Si el plugin trae su propio common/ (Proximidad, Espacialidad, Susurro), se usa ese;
# el resto comparte $ROOT/common (con el arreglo de presets de la 1.0.1).
if [ -d "$PLUGIN_DIR/common/obs" ]; then
	COMMON="$PLUGIN_DIR/common"
else
	COMMON="$ROOT/common"
fi
DEPS="${DEPS:-$ROOT/.deps}"
OUT="${OUT:-$ROOT/dist}"
BUILD="$ROOT/build/$PLUGIN_ID"
ZIG="python3 -m ziglang"
PLATFORMS="${SD_PLATFORMS:-${PLUGIN_PLATFORMS:-windows macos linux}}"
has() { [[ " $PLATFORMS " == *" $1 "* ]]; }
mkdir -p "$DEPS" "$OUT" "$BUILD"
log() { printf '\n\033[1;35m==> %s\033[0m\n' "$*"; }

# ------------------------------------------------------------------ dependencias
fetch_deps() {
	log "Dependencias (cache en $DEPS)"
	python3 -c "import ziglang, pefile, macholib, PIL" 2>/dev/null ||
		pip3 install --user --break-system-packages -q ziglang pefile macholib pillow ||
		pip3 install --user -q ziglang pefile macholib pillow
	if [ ! -f "$DEPS/obs-headers/libobs/obs-module.h" ]; then
		rm -rf "$DEPS/obs-headers"
		git clone -q --depth 1 --branch "$SD_OBS_HEADERS_TAG" --filter=blob:none --sparse \
			https://github.com/obsproject/obs-studio.git "$DEPS/obs-headers"
		git -C "$DEPS/obs-headers" sparse-checkout set libobs
	fi
	mkdir -p "$DEPS/config"
	cat >"$DEPS/config/obsconfig.h" <<CFG
#pragma once
/* obsconfig.h minimo para compilar plugins fuera del arbol de OBS */
#define OBS_VERSION "$SD_OBS_HEADERS_TAG"
#define OBS_VERSION_CANONICAL "$SD_OBS_HEADERS_TAG"
#define OBS_DATA_PATH "../../data"
#define OBS_INSTALL_PREFIX ""
#define OBS_PLUGIN_PATH "../../obs-plugins/64bit"
#define OBS_PLUGIN_DESTINATION "obs-plugins/64bit"
#define OBS_RELEASE_CANDIDATE 0
#define OBS_BETA 0
CFG
	local rel="https://github.com/obsproject/obs-studio/releases/download"
	if [ ! -f "$DEPS/obs28.dll" ]; then
		curl -fsSL -o "$DEPS/win28.zip" "$rel/28.0.0/OBS-Studio-28.0-Full-x64.zip"
		python3 - "$DEPS" <<'PY'
import sys, zipfile
d = sys.argv[1]; z = zipfile.ZipFile(d + '/win28.zip')
open(d + '/obs28.dll', 'wb').write(z.read('bin/64bit/obs.dll'))
PY
	fi
	if [ ! -f "$DEPS/obs-latest.dll" ] || [ ! -f "$DEPS/libobs-latest.so" ]; then
		curl -fsSL -o "$DEPS/win-latest.zip" "$rel/$SD_OBS_BINARIES_TAG/OBS-Studio-$SD_OBS_BINARIES_TAG-Windows-x64.zip"
		python3 - "$DEPS" <<'PY'
import sys, zipfile
d = sys.argv[1]; z = zipfile.ZipFile(d + '/win-latest.zip')
open(d + '/obs-latest.dll', 'wb').write(z.read('bin/64bit/obs.dll'))
PY
		curl -fsSL -o "$DEPS/obs-latest.deb" "$rel/$SD_OBS_BINARIES_TAG/OBS-Studio-$SD_OBS_BINARIES_TAG-Ubuntu-24.04-x86_64.deb" ||
			curl -fsSL -o "$DEPS/obs-latest.deb" "$rel/$SD_OBS_BINARIES_TAG/OBS-Studio-$SD_OBS_BINARIES_TAG-Ubuntu-26.04-x86_64.deb"
		(cd "$DEPS" && rm -rf debx && mkdir debx && cd debx && ar x ../obs-latest.deb &&
			tar xf data.tar.* --wildcards '*libobs.so*' && cp -L $(find . -name 'libobs.so.*[0-9]' | head -1) ../libobs-latest.so)
	fi
	# import lib de Windows generada desde el obs.dll de OBS 28.0 => solo simbolos de 28.0
	python3 "$ROOT/scripts/gen_stubs.py" def "$DEPS/obs28.dll" "$DEPS/obs28.def" >/dev/null
	$ZIG dlltool -m i386:x86-64 -d "$DEPS/obs28.def" -l "$DEPS/libobs28-win64.a"
	python3 "$ROOT/scripts/gen_stubs.py" tbd "$DEPS/libobs-latest.so" "$DEPS/libobs.tbd" >/dev/null
	if ! command -v makensis >/dev/null && [ ! -x "$DEPS/nsis/root/usr/bin/makensis" ]; then
		mkdir -p "$DEPS/nsis" && (cd "$DEPS/nsis" &&
			curl -fsSLO https://deb.debian.org/debian/pool/main/n/nsis/nsis_3.11-1_amd64.deb &&
			curl -fsSLO https://deb.debian.org/debian/pool/main/n/nsis/nsis-common_3.11-1_all.deb &&
			for f in *.deb; do dpkg-deb -x "$f" root; done) || echo "AVISO: NSIS no disponible; se omite el .exe"
	fi
}

# PLUGIN_CFLAGS / PLUGIN_CFLAGS_WINDOWS / PLUGIN_LDFLAGS_WINDOWS son opcionales (plugin.env)
# y conservan las flags del proyecto original cuando hacen falta para un binario identico.
CFLAGS_COMMON=(-O2 -std=gnu11 -Wall -Wextra -Wno-macro-redefined -fvisibility=hidden -ffp-contract=off
	-I"$DEPS/obs-headers/libobs" -I"$DEPS/config" -I"$COMMON" -I"$COMMON/dsp" -I"$COMMON/obs"
	-DPLUGIN_NAME="\"$PLUGIN_ID\"" -DPLUGIN_VERSION="\"$PLUGIN_VERSION\"")
# shellcheck disable=SC2206
[ -n "${PLUGIN_CFLAGS:-}" ] && CFLAGS_COMMON+=($PLUGIN_CFLAGS)
# Los plugins que nacieron como proyecto suelto compilaban con -Isrc
[ "$COMMON" = "$PLUGIN_DIR/common" ] && CFLAGS_COMMON+=(-I"$PLUGIN_DIR/src")
srcs() { for s in $PLUGIN_SOURCES; do echo "$PLUGIN_DIR/$s"; done; echo "$COMMON/obs/sd_presets.c"; }

# ------------------------------------------------------------------ pruebas DSP
run_tests() {
	log "Pruebas offline del DSP"
	local t=() ; for s in $PLUGIN_TESTS; do t+=("$PLUGIN_DIR/$s"); done
	$ZIG cc -O2 -std=gnu11 -I"$COMMON/dsp" -I"$PLUGIN_DIR/src" -o "$BUILD/dsp-test" "${t[@]}" -lm
	"$BUILD/dsp-test" | tee "$BUILD/dsp-test.log"
}

# ------------------------------------------------------------------ Linux
build_linux() {
	log "Linux x86_64 (.so)"
	local o="$BUILD/linux"; rm -rf "$o"; mkdir -p "$o"
	# Sin DT_NEEDED de libobs: los simbolos se resuelven contra la libobs ya cargada por OBS,
	# asi funciona con libobs.so.0 (OBS 28-29 / distros) y libobs.so.30 (OBS 30+ / Flatpak).
	$ZIG cc -target x86_64-linux-gnu.2.28 -fPIC -shared "${CFLAGS_COMMON[@]}" -s \
		-o "$o/$PLUGIN_ID.so" $(srcs) -lm
	nm -D --defined-only "$o/$PLUGIN_ID.so" | grep -q ' T obs_module_load' || { echo "falta obs_module_load"; exit 1; }
}

# ------------------------------------------------------------------ Windows
build_windows() {
	log "Windows x64 (.dll)"
	local o="$BUILD/windows"; rm -rf "$o"; mkdir -p "$o"
	IFS=. read -r V1 V2 V3 <<<"$PLUGIN_VERSION"
	sed -e "s|@V1@|$V1|g; s|@V2@|$V2|g; s|@V3@|$V3|g; s|@VERSION@|$PLUGIN_VERSION|g; s|@VENDOR@|$SD_VENDOR|g; s|@URL@|$SD_URL|g; s|@ID@|$PLUGIN_ID|g; s|@DISPLAY@|$PLUGIN_DISPLAY|g" \
		"$COMMON/windows/version.rc.in" >"$o/version.rc"
	win_cflags=()
	# shellcheck disable=SC2206
	[ -n "${PLUGIN_CFLAGS_WINDOWS:-}" ] && win_cflags+=($PLUGIN_CFLAGS_WINDOWS)
	win_ld=()
	# shellcheck disable=SC2206
	[ -n "${PLUGIN_LDFLAGS_WINDOWS:-}" ] && win_ld+=($PLUGIN_LDFLAGS_WINDOWS)
	$ZIG cc -target x86_64-windows-gnu -shared "${CFLAGS_COMMON[@]}" "${win_cflags[@]}" -s \
		-o "$o/$PLUGIN_ID.dll" $(srcs) "$o/version.rc" "$DEPS/libobs28-win64.a" "${win_ld[@]}"
	rm -f "$o"/*.pdb "$o"/*.lib
}

# ------------------------------------------------------------------ macOS
build_macos() {
	log "macOS universal (arm64 + x86_64 .plugin)"
	local o="$BUILD/macos"; rm -rf "$o"; mkdir -p "$o"
	for a in aarch64 x86_64; do
		$ZIG cc -target $a-macos.11.0 -shared "${CFLAGS_COMMON[@]}" \
			-o "$o/$PLUGIN_ID-$a" $(srcs) "$DEPS/libobs.tbd" \
			-Wl,-install_name,"@rpath/$PLUGIN_ID.plugin/Contents/MacOS/$PLUGIN_ID" \
			-Wl,-rpath,@executable_path/../Frameworks -Wl,-rpath,@loader_path/../../../../Frameworks
	done
	python3 "$ROOT/scripts/macho_lipo.py" "$o/$PLUGIN_ID" "$o/$PLUGIN_ID-aarch64" "$o/$PLUGIN_ID-x86_64"
	local b="$o/$PLUGIN_ID.plugin/Contents"
	mkdir -p "$b/MacOS" "$b/Resources"
	cp "$o/$PLUGIN_ID" "$b/MacOS/$PLUGIN_ID"; chmod 755 "$b/MacOS/$PLUGIN_ID"
	cp -r "$PLUGIN_DIR/data/." "$b/Resources/"
	cp "$ROOT/branding/icon.icns" "$b/Resources/icon.icns"
	sed -e "s|@ID@|$PLUGIN_ID|g; s|@DISPLAY@|$PLUGIN_DISPLAY|g; s|@BUNDLE_ID@|$PLUGIN_BUNDLE_ID|g; s|@VERSION@|$PLUGIN_VERSION|g; s|@VENDOR@|$SD_VENDOR|g; s|@URL@|$SD_URL|g" \
		"$ROOT/common/macos/Info.plist.in" >"$b/Info.plist"
	printf 'BNDL????' >"$b/PkgInfo"
}

# ------------------------------------------------------------------ verificacion
verify() {
	log "Verificacion de binarios"
	python3 "$ROOT/scripts/verify_binaries.py" "$BUILD" "$PLUGIN_ID" "$DEPS" | tee "$BUILD/verify.log"
}

# ------------------------------------------------------------------ empaquetado
package() {
	log "Empaquetado en $OUT"
	local base="$PLUGIN_PRODUCT-$PLUGIN_VERSION"
	local docs="$BUILD/docs"; rm -rf "$docs"; mkdir -p "$docs"
	cp "$ROOT/LICENSE" "$docs/LICENSE.txt"
	sed 's|(../../branding/banner.jpg)|(banner.jpg)|' "$PLUGIN_DIR/README.md" >"$docs/LEEME-README.md"
	cp "$ROOT/branding/banner.jpg" "$docs/banner.jpg"
	cp "$PLUGIN_DIR/CHANGELOG.md" "$docs/CHANGELOG.md"

	rm -f "$OUT/$base"-*
	if has windows; then
	# Windows: arbol listo para copiar sobre C:\Program Files\obs-studio
	local w="$BUILD/stage-windows"; rm -rf "$w"
	mkdir -p "$w/obs-plugins/64bit" "$w/data/obs-plugins/$PLUGIN_ID"
	cp "$BUILD/windows/$PLUGIN_ID.dll" "$w/obs-plugins/64bit/"
	cp -r "$PLUGIN_DIR/data/." "$w/data/obs-plugins/$PLUGIN_ID/"
	cp "$ROOT/branding/icon.ico" "$w/data/obs-plugins/$PLUGIN_ID/icon.ico"
	cp "$docs"/* "$w/"
	python3 "$ROOT/scripts/mkarchive.py" zip "$OUT/$base-windows.zip" "$w" ""

	local NSIS="makensis"; command -v makensis >/dev/null || { NSIS="$DEPS/nsis/root/usr/bin/makensis"; export NSISDIR="$DEPS/nsis/root/usr/share/nsis"; }
	if [ -x "$(command -v "$NSIS" || echo "$NSIS")" ]; then
		sed -e "s|@ID@|$PLUGIN_ID|g; s|@DISPLAY@|$PLUGIN_DISPLAY|g; s|@VERSION@|$PLUGIN_VERSION|g; s|@VENDOR@|$SD_VENDOR|g; s|@URL@|$SD_URL|g" \
			-e "s|@OUTFILE@|$OUT/$base-windows-installer.exe|g; s|@ICON@|$ROOT/branding/icon.ico|g" \
			-e "s|@WELCOME_BMP@|$ROOT/branding/nsis-welcome.bmp|g; s|@HEADER_BMP@|$ROOT/branding/nsis-header.bmp|g" \
			-e "s|@LICENSE@|$ROOT/LICENSE|g; s|@STAGE@|$w|g" \
			"$ROOT/common/windows/installer.nsi.in" >"$BUILD/installer.nsi"
		# NSIS en Linux acepta '/' en rutas File; normalizamos los '\' de la plantilla
		sed -i -E '/^\s*File /s|\\|/|g' "$BUILD/installer.nsi"
		"$NSIS" -V2 "$BUILD/installer.nsi"
	fi
	fi

	if has linux; then
	# Linux: ~/.config/obs-studio/plugins/<id>/{bin/64bit,data}
	local l="$BUILD/stage-linux/$PLUGIN_ID"; rm -rf "$BUILD/stage-linux"
	mkdir -p "$l/bin/64bit" "$l/data"
	cp "$BUILD/linux/$PLUGIN_ID.so" "$l/bin/64bit/"
	cp -r "$PLUGIN_DIR/data/." "$l/data/"
	cp "$ROOT/branding/logo-256.png" "$l/data/icon.png"
	sed -e "s|@ID@|$PLUGIN_ID|g; s|@DISPLAY@|$PLUGIN_DISPLAY|g" "$ROOT/common/linux/install.sh.in" >"$BUILD/stage-linux/install.sh"
	chmod 755 "$BUILD/stage-linux/install.sh"
	cp "$docs"/* "$BUILD/stage-linux/"
	python3 "$ROOT/scripts/mkarchive.py" tgz "$OUT/$base-linux-x86_64.tar.gz" "$BUILD/stage-linux" "$base-linux"
	fi

	if has macos; then
	# macOS: .plugin universal + instalador de doble clic
	local m="$BUILD/stage-macos"; rm -rf "$m"; mkdir -p "$m"
	cp -r "$BUILD/macos/$PLUGIN_ID.plugin" "$m/"
	sed -e "s|@ID@|$PLUGIN_ID|g; s|@DISPLAY@|$PLUGIN_DISPLAY|g" "$ROOT/common/macos/Instalar.command.in" >"$m/Instalar - Install.command"
	chmod 755 "$m/Instalar - Install.command"
	cp "$docs"/* "$m/"
	python3 "$ROOT/scripts/mkarchive.py" zip "$OUT/$base-macos-universal.zip" "$m" "$base-macos"
	fi

	# Codigo fuente completo
	python3 "$ROOT/scripts/mkarchive.py" src "$OUT/$base-source.zip" "$ROOT" "streamerdesk-asmr-suite-$PLUGIN_VERSION"
	(cd "$OUT" && rm -f "$base-SHA256SUMS.txt" && sha256sum $(ls "$base"-* | grep -v SHA256SUMS) >"$base-SHA256SUMS.txt.tmp" && mv "$base-SHA256SUMS.txt.tmp" "$base-SHA256SUMS.txt")
	ls -la "$OUT"
}

log "Plataformas: $PLATFORMS"
fetch_deps
run_tests
CAN_OBS_TEST=0
if [ "${RUN_OBS_TEST:-1}" = 1 ] && command -v gcc >/dev/null && command -v dpkg-deb >/dev/null; then CAN_OBS_TEST=1; fi
if has linux || [ "$CAN_OBS_TEST" = 1 ]; then build_linux; else rm -rf "$BUILD/linux"; fi
if has windows; then build_windows; else rm -rf "$BUILD/windows"; fi
if has macos; then build_macos; else rm -rf "$BUILD/macos"; fi
verify
if [ "$CAN_OBS_TEST" = 1 ]; then
	log "Prueba de integracion con libobs real (Linux)"
	TEST_SRC=""
	[ -f "$PLUGIN_DIR/test/obs_load_test.c" ] && TEST_SRC="$PLUGIN_DIR/test/obs_load_test.c"
	[ -z "$TEST_SRC" ] && [ -f "$PLUGIN_DIR/test/obs-integration/obs_load_test.c" ] && TEST_SRC="$PLUGIN_DIR/test/obs-integration/obs_load_test.c"
	OBS_TEST_DATA="$PLUGIN_DIR/data" DEPS="$DEPS" bash "$ROOT/scripts/run-obs-integration.sh" "$PLUGIN_ID" $TEST_SRC | tail -30
	grep -q "RESULTADO INTEGRACION: OK" "$BUILD/obs-integration.log" || { echo "Prueba de integracion FALLIDA"; exit 1; }
fi
package
log "OK"
