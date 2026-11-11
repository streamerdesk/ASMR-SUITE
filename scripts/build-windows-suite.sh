#!/usr/bin/env bash
# Compila los seis efectos para Windows x64 y genera los instaladores individuales
# (.exe + .zip) mas el instalador combinado de la suite 1.0.2.
# Pensado para CI (GitHub Actions / SignPath) y para reproducir los paquetes oficiales.
#
#   bash scripts/build-windows-suite.sh
#
# Variables: DEPS (cache de dependencias, por defecto .deps), OUT (por defecto dist).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
export DEPS="${DEPS:-$ROOT/.deps}"
export OUT="${OUT:-$ROOT/dist}"
export SD_PLATFORMS=windows
export RUN_OBS_TEST=0
cd "$ROOT"
mkdir -p "$OUT"
for p in plugins/pingpong-echo plugins/reverb plugins/8d-orbit \
         plugins/ear-to-ear plugins/proximity plugins/binaural; do
	echo
	echo "========== $p =========="
	bash "$ROOT/scripts/build-all.sh" "$p"
done
python3 "$ROOT/suite-bundle/build_bundle.py"
echo
echo "OK — paquetes Windows en $OUT"
ls -la "$OUT"/*.exe "$OUT"/*.zip "$OUT"/*SHA256SUMS* 2>/dev/null || ls -la "$OUT"
