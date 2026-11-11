#!/usr/bin/env bash
# Streamerdesk ASMR Suite - compila, verifica y empaqueta la version Linux x86_64 completa en dist-linux/
set -euo pipefail
cd "$(dirname "$0")/.."
python3 linux/build_linux_suite.py 2>&1 | tee build-linux/build.log
